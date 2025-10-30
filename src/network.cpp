#include <openssl/ssl.h>
#include <openssl/err.h>
#include <zlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <ctime>

#include <algorithm>
#include <array>
#include <iostream>
#include <fstream>
#include <memory>
#include <unordered_map>

#include "network.hpp"
#include "utils.hpp"

void network_init() {
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
}

static std::string escape(std::string_view source) {
	std::string output;
	for (char c : source) {
		if (c == '&') {
			output.append("&amp;");
		} else if (c == '<') {
			output.append("&lt;");
		} else if (c == '>') {
			output.append("&gt;");
		} else {
			output.push_back(c);
		}
	}
	return output;
}

std::optional<URL> URL::create(std::string_view string) {
	auto n = string.find(":");
	if (n == std::string::npos) {
		std::cerr << "Expected scheme in URL" << std::endl;
		return std::nullopt;
	}
	std::string scheme(string.substr(0, n));

	bool view_source = false;
	if (scheme == "view-source") {
		view_source = true;
		string = string.substr(n + 1);

		n = string.find(":");
		if (n == std::string::npos) {
			std::cerr << "Expected scheme after view-source in URL" << std::endl;
			return std::nullopt;
		}
		scheme = string.substr(0, n);
	}

	constexpr std::array supported_protocols{"http", "https", "file", "data", "about"};
	bool supported = std::find(supported_protocols.begin(), supported_protocols.end(), scheme) != supported_protocols.end();
	if (!supported) {
		std::cerr << "Unsupported protocol " << scheme << " in URL" << std::endl;
		return std::nullopt;
	}

	if (scheme == "data" || scheme == "about") {
		string = string.substr(n + 1);
	} else {
		if (string.substr(n + 1, 2) != "//") {
			std::cerr << "Expected '//' after scheme: in URL" << std::endl;
			return std::nullopt;
		}
		string = string.substr(n + 3);
	}

	if (scheme == "data") {
		n = string.find(",");
		if (n == std::string::npos) {
			std::cerr << "Expected ',' in data url" << std::endl;
			return std::nullopt;
		}
		if (string.substr(0, n) != "text/html") {
			std::cerr << "Unsupported MIME type " << string.substr(0, n) << " in URL" << std::endl;
			return std::nullopt;
		}
		// todo: make URL an enum (Rust style) or abstract class (OO style)
		// this isn't really what path is for...
		std::string path(string.substr(n + 1));
		return URL {
			.view_source = view_source,
			.scheme = scheme,
			.host = "",
			.port = 0,
			.path = path,
		};
	} else if (scheme == "about") {
		if (string != "blank") {
			std::cerr << "Unsupported about page " << string << " in URL" << std::endl;
			return std::nullopt;
		}
		return URL {
			.view_source = view_source,
			.scheme = scheme,
			.host = "blank",
			.port = 0,
			.path = "",
		};
	}

	std::string host, path;
	n = string.find("/");
	if (n == std::string::npos) {
		host = string;
		path = "/";
	} else {
		host = string.substr(0, n);
		path = string.substr(n);
	}

	n = host.find(":");
	uint16_t port;
	if (n != std::string::npos) {
		port = std::stoi(host.substr(n + 1));
		host = host.substr(0, n);
	} else if (scheme == "https") {
		port = 443;
	} else if (scheme == "http") {
		port = 80;
	} else if (scheme == "file") {
		port = 0;
	} else {
		assert(false && "unreachable");
	}

	if (scheme == "file") {
		assert(host == "");
		assert(port == 0);
	}

	return URL {
		.view_source = view_source,
		.scheme = scheme,
		.host = host,
		.port = port,
		.path = path,
	};
}

bool URL::operator==(const URL& other) const noexcept {
	return view_source == other.view_source && scheme == other.scheme && host == other.host && port == other.port && path == other.path;
}

// todo: special hash and eq impls for this?
URL URL::reusable_connection_subsection() const {
	return URL {
		.view_source = false,
		.scheme = scheme,
		.host = host,
		.port = port,
		.path = "",
	};
}

// todo: special hash and eq impls for this?
URL URL::cachable_subsection() const {
	return URL {
		.view_source = false,
		.scheme = scheme,
		.host = host,
		.port = port,
		.path = path,
	};
}

std::string URL::to_string() const {
	const char *source = (view_source) ? "view-source:" : "";
	return source + scheme + "://" + host + ":" + std::to_string(port) + path;
}

std::size_t std::hash<URL>::operator()(const URL& u) const noexcept {
	size_t seed = 0;
	combine_hash(seed, std::hash<size_t>{}(u.view_source));
	combine_hash(seed, std::hash<std::string>{}(u.scheme));
	combine_hash(seed, std::hash<std::string>{}(u.host));
	combine_hash(seed, std::hash<uint16_t>{}(u.port));
	combine_hash(seed, std::hash<std::string>{}(u.path));
	return seed;
}

struct HttpResponse {
	int status;
	std::string version;
	std::string explanation;
	std::unordered_map<std::string, std::string> headers;
	std::string body;
};

class HttpConnection {
	int m_socket_fd;
	SSL_CTX *m_ctx = nullptr;
	SSL *m_ssl = nullptr;

public:
	HttpConnection(char const *host, uint16_t port, bool encrypt) {
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		std::string port_s = std::to_string(port);

		addrinfo *res;
		int status = getaddrinfo(host, port_s.c_str(), &hints, &res);
		if (status != 0) {
			assert(false);
		}

		m_socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (m_socket_fd == -1) {
			perror("socket");
			freeaddrinfo(res);
			assert(false);
		}

		int connection_result = connect(m_socket_fd, res->ai_addr, res->ai_addrlen);
		freeaddrinfo(res);
		if (connection_result == -1) {
			perror("connect");
			close(m_socket_fd);
			assert(false);
		}

		if (encrypt) {
			const SSL_METHOD *method = TLS_client_method();
			m_ctx = SSL_CTX_new(method);
			if (!m_ctx) {
				ERR_print_errors_fp(stderr);
				close(m_socket_fd);
				assert(false);
			}

			m_ssl = SSL_new(m_ctx);
			if (!m_ssl) {
				ERR_print_errors_fp(stderr);
				SSL_CTX_free(m_ctx);
				close(m_socket_fd);
				assert(false);
			}
			if (SSL_set_fd(m_ssl, m_socket_fd) == 0) {
				ERR_print_errors_fp(stderr);
				SSL_free(m_ssl);
				SSL_CTX_free(m_ctx);
				close(m_socket_fd);
				assert(false);
			}

			if (SSL_set_tlsext_host_name(m_ssl, host) == 0) {
				ERR_print_errors_fp(stderr);
				SSL_free(m_ssl);
				SSL_CTX_free(m_ctx);
				close(m_socket_fd);
				assert(false);
			}

			int connection_status = SSL_connect(m_ssl);
			if (connection_status == 0) {
				// gracefully failed
				ERR_print_errors_fp(stderr);
				SSL_free(m_ssl);
				SSL_CTX_free(m_ctx);
				close(m_socket_fd);
				assert(false);
			} else if (connection_status < 0) {
				ERR_print_errors_fp(stderr);
				SSL_free(m_ssl);
				SSL_CTX_free(m_ctx);
				close(m_socket_fd);
				assert(false);
			}
		}
	}

	~HttpConnection() {
		if (is_encrypted()) {
			auto ret = SSL_shutdown(m_ssl);
			if (ret < 0) {
				//SSL_get_error(m_ssl, ret);
				assert(false);
			} else if (ret == 0) {
				// no error, shutdown in progress
			}
			SSL_free(m_ssl);
			SSL_CTX_free(m_ctx);
		}
		close(m_socket_fd);
	}

	bool is_encrypted() const {
		return m_ssl != nullptr;
	}

	HttpResponse request(URL url) {
		std::string request = "GET " + url.path + " HTTP/1.1\r\n";

		// todo: idk why i can't make the second paramter a string_view as well...
		// maybe it's make_pair? it doesn't do the conversion well? idk
		std::vector<std::pair<std::string_view, std::string>> request_headers;
		request_headers.push_back(std::make_pair("Host", url.host));
		request_headers.push_back(std::make_pair("Connection", "keep-alive"));
		request_headers.push_back(std::make_pair("User-Agent", "ladybug 1.0"));
		request_headers.push_back(std::make_pair("Accept-Encoding", "gzip, deflate"));
		for (auto pair : request_headers) {
			request.append(pair.first);
			request.append(": ");
			request.append(pair.second);
			request.append("\r\n");
		}
		request.append("\r\n");
		write(request);

		int const buffer_size = 4096;
		char input_buffer[buffer_size];
		char *next_in;
		int avail_in;
		HttpResponse response = parse_up_to_body(input_buffer, buffer_size, next_in, avail_in);
		parse_body(input_buffer, buffer_size, next_in, avail_in, response);

		return response;
	}

	HttpResponse parse_up_to_body(char *input_buffer, int input_buffer_size, char *& next_in, int& avail_in) {

		enum ParsingState {
			PARSING_STATUS_LINE,
			PARSING_HEADERS,
			PARSING_BODY,
		};

		std::string received;
		ParsingState parsing_state = PARSING_STATUS_LINE;
		HttpResponse response;

		int bytes_received;
		do {
			bytes_received = read(input_buffer, input_buffer_size);
			if (bytes_received == 0) {
				break;
			}
			received.append(input_buffer, bytes_received);

			for (;;) {

				auto line_end = received.find("\r\n");
				if (line_end == std::string::npos) {
					break;
				}

				std::string line = received.substr(0, line_end);
				received = received.substr(line_end + 2);
				if (parsing_state == PARSING_STATUS_LINE) {
					auto status = split(line, " ", 2);
					assert(status.size() == 3);

					response.version = status[0];
					response.status = std::stoi(std::string{status[1]});
					response.explanation = status[2];

					parsing_state = PARSING_HEADERS;
				} else if (parsing_state == PARSING_HEADERS) {
					if (line == "") {
						parsing_state = PARSING_BODY;
						break;
					} else {
						auto header_split = split(line, ":", 1);
						assert(header_split.size() == 2);

						std::string header_name { header_split[0] };
						std::string_view header_value = header_split[1];
						std::transform(header_name.begin(), header_name.end(), header_name.begin(), ::tolower);

						header_value = trim_whitespace(header_value);
						response.headers.insert({header_name, std::string{header_value}});
					}
				} else {
					assert(false);
				}
			}
		} while (parsing_state != PARSING_BODY);

		int data_offset = bytes_received - received.length();
		next_in = input_buffer + data_offset;
		avail_in = received.length();

		return response;
	}

	void parse_body(char *input_buffer, int input_buffer_size, char *next_in, int avail_in, HttpResponse& response) {
		int total_content_length = -1;
		if (auto content_length_entry = response.headers.find("content-length"); content_length_entry != response.headers.end()) {
			total_content_length = std::stol(content_length_entry->second);
		}

		bool decompress = false;
		z_stream zstream;

		if (auto content_encoding = response.headers.find("content-encoding"); content_encoding != response.headers.end()) {
			if (content_encoding->second == "gzip") {
				decompress = true;
			} else if (content_encoding->second == "deflate") {
				decompress = true;
			}
		}

		bool chunked_transfer = false;
		if (auto transfer_encoding = response.headers.find("transfer-encoding"); transfer_encoding != response.headers.end()) {
			std::vector<std::string_view> s = split(transfer_encoding->second, ",");
			for (auto v : s) {
				std::string_view value = trim_whitespace(v);
				if (value == "gzip") {
					decompress = true;
				} else if (value == "deflate") {
					decompress = true;
				} else if (value == "chunked") {
					chunked_transfer = true;
				} else {
					assert(false);
				}
			}
		}

		assert(total_content_length != -1 || chunked_transfer);

		if (decompress) {
			std::cerr << "Uncompressing body" << std::endl;
			zstream.zalloc = Z_NULL;
			zstream.zfree = Z_NULL;
			zstream.opaque = Z_NULL;
			zstream.avail_in = 0;
			zstream.next_in = Z_NULL;
			// allow gzip and zlib wrappers
			int ret = inflateInit2(&zstream, 15 + 32);
			assert(ret == Z_OK);
		}

		int current_content_length = 0;
		int size_of_current_chunk = -1;

		if (chunked_transfer) {
			// per chunk
			do {
				size_of_current_chunk = 0;
				current_content_length = 0;
				// odd loop, in practice only runs twice, nullptr then not
				for (;;) {
					char *newline = (char *)memmem(next_in, avail_in, "\r\n", 2);
					if (newline == nullptr) {
						int length = avail_in;
						assert(next_in[avail_in - 1] != '\r' && "Not handling split \\r\\n!");
						std::string s(next_in, length);
						if (s != "") {
							int adding = std::stoi(s, 0, 16);
							size_of_current_chunk = (size_of_current_chunk << length) + adding;
						}

						avail_in = read(input_buffer, input_buffer_size);
						next_in = input_buffer;
					} else {
						int length = newline - next_in;
						std::string s(next_in, length);
						if (s != "") {
							int adding = std::stoi(s, 0, 16);
							size_of_current_chunk = (size_of_current_chunk << length) + adding;
						}

						next_in += length + 2;
						avail_in -= length + 2;
						break;
					}
				}

				// we break in the middle of this loop because of the data we start with
				do {
					int amount_to_read = std::min(size_of_current_chunk - current_content_length, avail_in);

					decode_exact(next_in, amount_to_read, decompress, &zstream, response);

					if (amount_to_read == avail_in) {
						avail_in = read(input_buffer, input_buffer_size);
						next_in = input_buffer;
					} else {
						next_in += amount_to_read;
						avail_in -= amount_to_read;
					}

					current_content_length += amount_to_read;
				} while (current_content_length != size_of_current_chunk);

				if (avail_in >= 2) {
					assert(next_in[0] == '\r');
					assert(next_in[1] == '\n');
					next_in += 2;
					avail_in -= 2;
				} else {
					assert(false && "Not handling split \\r\\n!");
				}

			} while (size_of_current_chunk != 0);
		} else {
			// we break in the middle of this loop because of the data we start with
			for (;;) {

				int amount_to_read = std::min(total_content_length - current_content_length, avail_in);

				decode_exact(next_in, amount_to_read, decompress, &zstream, response);

				current_content_length += amount_to_read;
				if (current_content_length == total_content_length) {
					break;
				}

				if (amount_to_read == avail_in) {
					avail_in = read(input_buffer, input_buffer_size);
					next_in = input_buffer;

					if (avail_in == 0) {
						break;
					}
				} else {
					next_in += amount_to_read;
					avail_in -= amount_to_read;
				}
			}
		}
	}

private:
	void decode_exact(char *next_in, int avail_in, bool decompress, z_stream *zstream, HttpResponse& response) {
		if (decompress) {
			int const output_buffer_size = 4096;
			char output_buffer[output_buffer_size];
			zstream->avail_in = avail_in;
			zstream->next_in = (unsigned char *)next_in;
			do {
				zstream->avail_out = output_buffer_size;
				zstream->next_out = (unsigned char *)output_buffer;
				int ret = inflate(zstream, Z_NO_FLUSH);
				assert(ret != Z_STREAM_ERROR);
				switch (ret) {
				case Z_NEED_DICT:
					ret = Z_DATA_ERROR;
					[[fallthrough]];
				case Z_DATA_ERROR:
				case Z_MEM_ERROR:
					inflateEnd(zstream);
					// return ret; // from docs
					assert(false);
				}

				int have = output_buffer_size - zstream->avail_out;
				response.body.append(output_buffer, have);
			} while (zstream->avail_out == 0);
		} else {
			response.body.append(next_in, avail_in);
		}
	}

	void write(std::string data) {
		if (is_encrypted()) {
			// there may be handleable failures here for larger request sizes?
			int send_result = SSL_write(m_ssl, data.data(), data.size());
			if (send_result <= 0) {
				ERR_print_errors_fp(stderr);
				SSL_shutdown(m_ssl);
				SSL_free(m_ssl);
				SSL_CTX_free(m_ctx);
				close(m_socket_fd);
				assert(false);
			}
		} else {
			int send_result = send(m_socket_fd, data.data(), data.size(), 0);
			if (send_result == -1) {
				perror("send");
				assert(false);
			}
		}
	}

	int read(char *buffer, int length) const {
		if (is_encrypted()) {
			int bytes_received = SSL_read(m_ssl, buffer, length);
			if (bytes_received <= 0) {
				switch (SSL_get_error(m_ssl, bytes_received)) {
				case SSL_ERROR_ZERO_RETURN:
					return 0;
				default:
					// i can't use goto for error handling this is tragic?
					// do i have to... use deconstructors?
					ERR_print_errors_fp(stderr);
					SSL_shutdown(m_ssl);
					SSL_free(m_ssl);
					SSL_CTX_free(m_ctx);
					close(m_socket_fd);
					assert(false);
					break;
				}
			}
			return bytes_received;
		} else {
			int bytes_received = recv(m_socket_fd, buffer, length, 0);
			assert(bytes_received != -1);
			return bytes_received;
		}
	}
};

struct CachedHttpResponse {
	HttpResponse response;
	std::time_t expires_at;
};

ConnectionManager::ConnectionManager() = default;
ConnectionManager::~ConnectionManager() = default;

std::string ConnectionManager::request(URL url) {
	std::string response;
	if (url.scheme == "file") {
		response = load_file(url);
	} else if (url.scheme == "data") {
		response = url.path;
	} else if (url.scheme == "http" || url.scheme == "https") {
		response = load_from_cache_or_fetch(url);
	} else if (url.scheme == "about") {
		if (url.host == "blank") {
			response = "";
		} else {
			assert(false && "unreachable");
		}
	} else {
		assert(false && "unreachable");
	}

	if (url.view_source) {
		return escape(response);
	} else {
		return response;
	}
}

void ConnectionManager::print_active_connections() const {
	for (auto const& p : m_active_connections) {
		std::cerr << p.first.to_string() << std::endl;
	}
}

std::string ConnectionManager::load_file(URL url) const {
	std::ifstream file(url.path);
	if (!file.is_open()) {
		std::cerr << "Invalid path" << std::endl;
		exit(1);
	}

	// huh, second paramater for what?
	std::string file_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();
	return file_content;
}

std::string ConnectionManager::load_from_cache_or_fetch(URL url) {
	// todo: custom hash impl? is this even worth?
	URL cachable_url = url.cachable_subsection();
	if (auto cached = m_cached_responses.find(cachable_url); cached != m_cached_responses.end()) {
		std::time_t now = std::time(nullptr);
		if (now >= cached->second->expires_at) {
			m_cached_responses.erase(cached);
		} else {
			std::cerr << "Using cached response for " << cachable_url.to_string() << std::endl;
			return cached->second->response.body;
		}
	}
	// todo: move caching to after this step?
	return request_http(url);
}

void ConnectionManager::store_in_cache_if_cachable(URL url, HttpResponse response) {
	// todo: add 301, 404, etc.
	if (response.status != 200) {
		return;
	}

	auto ctrl_header = response.headers.find("cache-control");
	if (ctrl_header == response.headers.end()) {
		return;
	}

	std::vector<std::string_view> directives = split(ctrl_header->second, ",");
	for (auto d : directives) {
		std::string_view directive = trim_whitespace(d);

		if (directive == "no-store") {
			std::cerr << "No store for " << url.to_string() << std::endl;
			return;
		}

		std::string_view header_start = "max-age=";
		if (directive.rfind(header_start, 0) != 0) { // does not starts_with
			// todo: handle other values
			return;
		}

		// this function throws on failure :(
		// todo: no std::string
		uint64_t max_age = std::stol(std::string{directive.substr(header_start.length())});

		uint64_t age = 0;
		if (auto age_finder = response.headers.find("age"); age_finder != response.headers.end()) {
			age = std::stol(age_finder->second);
		}

		std::time_t expires_at = std::time(nullptr) + max_age - age;

		URL cachable_url = url.cachable_subsection();
		auto cachable_response = std::make_unique<CachedHttpResponse>(CachedHttpResponse {
			.response = response,
			.expires_at = expires_at,
		});
		m_cached_responses.insert({cachable_url, std::move(cachable_response)});
	}
}

std::string ConnectionManager::request_http(URL url) {
	// redirect loop
	for (int i = 0; i < 10; i++) {
		URL reusable_base = url.reusable_connection_subsection();
		auto pair = m_active_connections.find(reusable_base);

		// I wish if statements were expressions so bad
		HttpConnection *connection = [&] {
			if (pair == m_active_connections.end()) {
				std::cerr << "Creating new connection for " << reusable_base.to_string() << std::endl;
				bool encrypted;
				if (url.scheme == "http") {
					encrypted = false;
				} else if (url.scheme == "https") {
					encrypted = true;
				} else {
					assert(false);
				}
				auto connection = std::make_unique<HttpConnection>(url.host.c_str(), url.port, encrypted);
				m_active_connections.insert({reusable_base, std::move(connection)});
				HttpConnection *conn = m_active_connections[reusable_base].get();
				return conn;
			} else {
				std::cerr << "Reusing connection for " << reusable_base.to_string() << std::endl;
				return pair->second.get();
			}
		}();

		HttpResponse response = connection->request(url);
		if (response.status >= 300 && response.status < 400) {
			std::string location = response.headers["location"];
			if (location.rfind("/", 0) == 0) { // starts_with
				url.path = location;
			} else {
				url = URL::create(location).value_or(URL::create("about:blank").value());
			}
			std::cerr << "Redirected to " << url.to_string() << std::endl;
			continue;
		}

		store_in_cache_if_cachable(url, response);

		return response.body;
	}

	assert(false && "Too many redirects");
}
