#include <charconv>
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
#include <memory>
#include <sstream>
#include <unordered_map>

#include "network.hpp"
#include "utils.hpp"

void network_init() {
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
}

std::string url_encode(std::string_view s) {
	std::string out;
	for (char c : s) {
		if (!std::isalnum(c) && c != '-' && c != '_' && c !='.' && c != '~') {
			out.push_back('%');
			std::stringstream ss;
			ss << std::hex << (int) c;
			std::string t = ss.str();
			std::transform(t.begin(), t.end(), t.begin(), ::toupper);
			out.append(t);
		} else {
			out.push_back(c);
		}
	}
	return out;
}

static std::optional<URL> parse_data_url(bool view_source, std::string scheme, std::string_view string) {
	size_t n = string.find(",");
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
		.fragment = "",
	};
}

static std::optional<URL> parse_about_url(bool view_source, std::string scheme, std::string_view string) {
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
		.fragment = "",
	};
}

static std::optional<URL> parse_relative_file_url(bool view_source, std::string scheme, std::string_view string) {
	return URL {
		.view_source = view_source,
		.scheme = scheme,
		.host = "",
		.port = 0,
		.path = std::string(string),
		.fragment = "",
	};
}

static std::optional<URL> parse_standard_url(bool view_source, std::string scheme, std::string_view string) {
	std::string host, path, fragment;
	size_t n = string.find('/');
	if (n == std::string::npos) {
		host = string;
		path = "/";
	} else {
		host = string.substr(0, n);
		path = string.substr(n);
	}
	n = path.find('#');
	if (n != std::string::npos) {
		fragment = path.substr(n + 1);
		path = path.substr(0, n);
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

	if (scheme == "file" && (!host.empty() || port == 0)) {
		std::cerr << "`file` URL should have neither a host or port" << std::endl;
		return std::nullopt;
	}

	return URL {
		.view_source = view_source,
		.scheme = scheme,
		.host = host,
		.port = port,
		.path = path,
		.fragment = fragment,
	};
}

std::optional<URL> URL::create(std::string_view string) {
	auto n = string.find(":");
	if (n == std::string::npos) {
		// todo: default to https
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
	string = string.substr(n + 1);

	constexpr std::array supported_protocols{"http", "https", "file", "data", "about"};
	bool supported = std::find(supported_protocols.begin(), supported_protocols.end(), scheme) != supported_protocols.end();
	if (!supported) {
		std::cerr << "Unsupported protocol " << scheme << " in URL" << std::endl;
		return std::nullopt;
	}

	if (scheme == "data") {
		return parse_data_url(view_source, std::move(scheme), string);
	} else if (scheme == "about") {
		return parse_about_url(view_source, std::move(scheme), string);
	} else if (scheme == "file") {
		if (string.starts_with("//")) {
			return parse_standard_url(view_source, std::move(scheme), string.substr(2));
		} else {
			return parse_relative_file_url(view_source, std::move(scheme), string);
		}
	} else if (scheme == "http" || scheme == "https") {
		if (!string.starts_with("//")) {
			std::cerr << "Expected '//' after scheme: in " << scheme << " URL" << std::endl;
			return std::nullopt;
		}
		string = string.substr(2);
		return parse_standard_url(view_source, std::move(scheme), string);
	} else {
		assert(false && "unreachable");
	}
}

URL URL::ABOUT_BLANK = *URL::create("about:blank");

std::optional<URL> URL::resolve(std::string_view url_) const {
	if (url_.find("://") != std::string::npos) {
		// url is abosolute
		return URL::create(url_);
	}
	std::string url { url_ };
	if (url.starts_with('#')) {
		URL out = *this;
		out.fragment = url.substr(1);
		return out;
	} else if (!url.starts_with('/')) {
		size_t dir_end = path.rfind("/");
		std::string_view dir { path.substr(0, dir_end) };

		while (url.starts_with("../")) {
			size_t new_url_start = url.find("/");
			assert(new_url_start != std::string::npos);
			url = url.substr(new_url_start);
			if (dir.find("/") != std::string::npos) {
				dir_end = path.rfind("/");
				dir = dir.substr(0, dir_end);
			}
		}

		url.insert(0, "/");
		url.insert(0, dir);
	}
	if (url.starts_with("//")) {
		return URL::create(scheme + "://" + url);
	} else {
		return URL::create(scheme + "://" + host + ":" + std::to_string(port) + url);
	}
}

bool URL::equal_disregarding_fragment(URL const& other) const {
	return view_source == other.view_source && scheme == other.scheme && host == other.host && port == other.port && path == other.path;
}

bool URL::operator==(const URL& other) const noexcept {
	return equal_disregarding_fragment(other) && fragment == other.fragment;
}

// todo: special hash and eq impls for this?
URL URL::reusable_connection_subsection() const {
	return URL {
		.view_source = false,
		.scheme = scheme,
		.host = host,
		.port = port,
		.path = "",
		.fragment = "",
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
		.fragment = "",
	};
}

std::ostream& operator<<(std::ostream& os, URL const& url) {
	char const *source = (url.view_source) ? "view-source:" : "";
	char const *scheme_delimeter = [&]() {
		if (url.scheme == "https" || url.scheme == "http" || (url.scheme == "file" && !url.path.starts_with('.'))) {
			return "://";
		} else {
			return ":";
		}
	}();
	auto port = [&]() -> std::optional<uint16_t> {
		if (url.scheme == "https" && url.port == 443) {
			return std::nullopt;
		} else if (url.scheme == "http" && url.port == 80) {
			return std::nullopt;
		} else if (url.scheme == "file" || url.scheme == "data" || url.scheme == "about") {
			return std::nullopt;
		} else {
			return url.port;
		}
	}();

	os << source << url.scheme << scheme_delimeter << url.host;
	if (port) {
		os << ":" << *port;
	}
	os << url.path;
	if (!url.fragment.empty()) {
		os << "#" << url.fragment;
	}
	return os;
}

std::size_t std::hash<URL>::operator()(const URL& u) const noexcept {
	size_t seed = 0;
	combine_hash(seed, std::hash<bool>{}(u.view_source));
	combine_hash(seed, std::hash<std::string>{}(u.scheme));
	combine_hash(seed, std::hash<std::string>{}(u.host));
	combine_hash(seed, std::hash<uint16_t>{}(u.port));
	combine_hash(seed, std::hash<std::string>{}(u.path));
	combine_hash(seed, std::hash<std::string>{}(u.fragment));
	return seed;
}

struct HttpResponse {
	int status;
	std::string version;
	std::string explanation;
	std::unordered_map<std::string, std::string> headers;
	std::string body;
};

// todo: This class has a lot of asserts around reading and validating data.
// Replacing these errors involves delving deeper into if they are:
// - Recoverable: The method can problem solve and produce a result anyway
// - Unrecoverable: The method must report failure to its caller
// - Fatal: The method must communicate that this HttpConnection
//   is no longer usable and should be destroyed (i.e. removed from the
//   reusable connection cache).
// This is currently a lot of work at this stage, and since we will also be handling
// POST request soon instead of just GET, I'm going to defer this refactor. It will
// undoubtedly complicated the code so I would rather add the necessary features and
// then refactor.
class HttpConnection {
	int m_socket_fd;
	SSL_CTX *m_ctx;
	SSL *m_ssl;

public:
	static std::optional<std::unique_ptr<HttpConnection>> create(char const *host, uint16_t port, bool encrypt) {
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		std::string port_s = std::to_string(port);
		int connection_result{}, socket_fd{};
		SSL_CTX *ctx = nullptr;
		SSL *ssl = nullptr;

		addrinfo *res;
		int status = getaddrinfo(host, port_s.c_str(), &hints, &res);
		if (status != 0) {
			goto failure;
		}

		socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (socket_fd == -1) {
			perror("socket");
			freeaddrinfo(res);
			goto failure;
		}

		connection_result = connect(socket_fd, res->ai_addr, res->ai_addrlen);
		freeaddrinfo(res);
		if (connection_result == -1) {
			perror("connect");
			goto failure_close;
		}

		if (encrypt) {
			const SSL_METHOD *method = TLS_client_method();
			ctx = SSL_CTX_new(method);
			if (!ctx) {
				ERR_print_errors_fp(stderr);
				goto failure_close;
			}

			ssl = SSL_new(ctx);
			if (!ssl) {
				ERR_print_errors_fp(stderr);
				goto failure_ctx_free;
			}
			if (SSL_set_fd(ssl, socket_fd) == 0) {
				ERR_print_errors_fp(stderr);
				goto failure_ssl_free;
			}

			if (SSL_set_tlsext_host_name(ssl, host) == 0) {
				ERR_print_errors_fp(stderr);
				goto failure_ssl_free;
			}

			int connection_status = SSL_connect(ssl);
			if (connection_status == 0) {
				// gracefully failed
				ERR_print_errors_fp(stderr);
				goto failure_ssl_free;
			} else if (connection_status < 0) {
				ERR_print_errors_fp(stderr);
				goto failure_ssl_free;
			}
		}
		return std::make_unique<HttpConnection>(socket_fd, ctx, ssl);

	failure_ssl_free:
		SSL_free(ssl);
	failure_ctx_free:
		SSL_CTX_free(ctx);
	failure_close:
		close(socket_fd);
	failure:
		// todo: optional!
		return std::nullopt;
	}

	HttpConnection(int socket_fd, SSL_CTX *ctx, SSL *ssl)
		: m_socket_fd(socket_fd)
		, m_ctx(ctx)
		, m_ssl(ssl)
	{}

	~HttpConnection() {
		if (is_encrypted()) {
			auto ret = SSL_shutdown(m_ssl);
			if (ret < 0) {
				// todo: 
				// SSL_get_error(m_ssl, ret);
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

	std::optional<HttpResponse> request(URL url, std::optional<std::string> payload) {
		std::string method { payload ? "POST" : "GET" };
		std::string request = method + " " + url.path + " HTTP/1.1\r\n";

		std::vector<std::pair<std::string_view, std::string_view>> request_headers;
		request_headers.push_back(std::make_pair("Host", std::string_view{url.host}));
		request_headers.push_back(std::make_pair("Connection", "keep-alive"));
		request_headers.push_back(std::make_pair("User-Agent", "ladybug 1.0"));
		request_headers.push_back(std::make_pair("Accept-Encoding", "gzip, deflate"));
		if (payload) {
			request_headers.push_back(std::make_pair("Content-Length", std::to_string(payload->size())));
		}
		for (auto const& pair : request_headers) {
			request.append(pair.first);
			request.append(": ");
			request.append(pair.second);
			request.append("\r\n");
		}
		request.append("\r\n");
		if (payload) {
			request.append(*payload);
		}

		if (!write(request)) {
			std::cerr << "Writing request failed" << std::endl;
			return std::nullopt;
		}

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
						make_lowercase(header_name);

						header_value = trim_whitespace(header_value);
						// todo: Handle multiple headers of the same value.
						// I don't think even some HTTP packages handle this correctly, so I don't think
						// it is a huge obstacle for browsing the web. That being said std::unordered_multimap
						// does exist and probably isn't that much of a headach
						response.headers.insert({header_name, std::string{header_value}});
					}
				} else {
					assert(false && "unreachable");
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
			// todo: no more exceptions!
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
					// todo: error
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

		if (decompress) {
			inflateEnd(&zstream);
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

	bool write(std::string data) {
		if (is_encrypted()) {
			// todo: there may be handleable failures here for larger request sizes?
			int send_result = SSL_write(m_ssl, data.data(), data.size());
			if (send_result <= 0) {
				ERR_print_errors_fp(stderr);
				SSL_shutdown(m_ssl);
				SSL_free(m_ssl);
				SSL_CTX_free(m_ctx);
				close(m_socket_fd);
				return false;
			}
		} else {
			int send_result = send(m_socket_fd, data.data(), data.size(), 0);
			if (send_result == -1) {
				perror("send");
				return false;
			}
		}
		return true;
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
	std::optional<std::time_t> expires_at;
};

ConnectionManager::ConnectionManager() = default;
ConnectionManager::~ConnectionManager() = default;

std::optional<std::string> ConnectionManager::request(URL url, std::optional<std::string> payload) {
	std::optional<std::string> response;
	if (url.scheme == "file") {
		response = load_file(url);
	} else if (url.scheme == "data") {
		response = url.path;
	} else if (url.scheme == "http" || url.scheme == "https") {
		response = load_http_or_from_cache(url, payload);
	} else if (url.scheme == "about") {
		if (url.host == "blank") {
			response = "";
		} else {
			std::cerr << "Unsupported about url" << std::endl;
		}
	} else {
		assert(false && "unreachable");
	}

	if (url.view_source && response) {
		return escape(*response);
	} else {
		return response;
	}
}

void ConnectionManager::print_active_connections() const {
	for (auto const& p : m_active_connections) {
		std::cerr << p.first << std::endl;
	}
}

std::optional<std::string> ConnectionManager::load_file(URL url) const {
	return read_entire_file_to_string(url.path);
}

std::optional<std::string> ConnectionManager::try_load_from_cache(URL url) {
	URL cachable_url = url.cachable_subsection();
	if (auto cached = m_cached_responses.find(cachable_url); cached != m_cached_responses.end()) {
		if (auto expires_at = cached->second->expires_at; expires_at && std::time(nullptr) >= *expires_at) {
			m_cached_responses.erase(cached);
		} else {
			std::cerr << "Using cached response for " << cachable_url << std::endl;
			return cached->second->response.body;
		}
	}
	return std::nullopt;
}

static std::pair<bool, std::optional<std::time_t>> response_is_cachable(HttpResponse const& response) {
	// todo: add 301, 404, etc.
	if (response.status != 200) {
		return { false, std::nullopt };
	}

	auto ctrl_header = response.headers.find("cache-control");
	if (ctrl_header == response.headers.end()) {
		// fair game
		return { true, std::nullopt };
	}

	std::vector<std::string_view> directives = split(ctrl_header->second, ",");
	std::optional<uint64_t> max_age{};
	for (auto d : directives) {
		std::string_view directive = trim_whitespace(d);

		if (directive == "no-store") {
			return { false, std::nullopt };
		}

		std::string_view header_start = "max-age=";
		// todo: handle other values
		if (!directive.starts_with(header_start)) {
			return { false, std::nullopt };
		}

		assert(std::from_chars(directive.data() + header_start.size(), directive.data() + directive.size(), *max_age).ec == std::errc{});
	}

	if (max_age) {
		uint64_t age = 0;
		if (auto age_finder = response.headers.find("age"); age_finder != response.headers.end()) {
			age = std::stol(age_finder->second);
		}

		std::time_t expires_at = std::time(nullptr) + *max_age - age;
		return { true, expires_at };
	} else {
		return { true , std::nullopt };
	}

}

void ConnectionManager::store_in_cache_if_cachable(URL url, HttpResponse response) {
	auto [is_cachable, expires_at] = response_is_cachable(response);
	if (!is_cachable) {
		return;
	}

	URL cachable_url = url.cachable_subsection();
	auto cachable_response = std::make_unique<CachedHttpResponse>(CachedHttpResponse {
		.response = response,
		.expires_at = expires_at,
	});
	m_cached_responses.insert({cachable_url, std::move(cachable_response)});
}

std::optional<std::string> ConnectionManager::load_http_or_from_cache(URL url, std::optional<std::string> payload) {
	// Redirect loop.
	for (int i = 0; i < 10; i++) {
		if (auto content = try_load_from_cache(url)) {
			return *content;
		}

		URL reusable_base = url.reusable_connection_subsection();
		auto pair = m_active_connections.find(reusable_base);

		// I wish if statements were expressions so bad
		auto connection = [&]() -> HttpConnection* {
			if (pair == m_active_connections.end()) {
				std::cerr << "Creating new connection for " << reusable_base << " (" << url << ")" << std::endl;
				bool encrypted;
				if (url.scheme == "http") {
					encrypted = false;
				} else if (url.scheme == "https") {
					encrypted = true;
				} else {
					assert(false);
				}
				auto connection { HttpConnection::create(url.host.c_str(), url.port, encrypted) };
				if (connection) {
					m_active_connections.insert({reusable_base, std::move(*connection)});
					HttpConnection *conn = m_active_connections[reusable_base].get();
					return conn;
				} else {
					std::cerr << "Connection to " << url << " failed" << std::endl;
					return nullptr;
				}
			} else {
				std::cerr << "Reusing connection for " << reusable_base << " (" << url << ")" << std::endl;
				return pair->second.get();
			}
		}();
		if (!connection) {
			return std::nullopt;
		}

		auto response = connection->request(url, payload);
		if (!response) {
			return std::nullopt;
		}
		std::cerr << response->status << " to " << url << std::endl;
		if (auto conn = response->headers.find("connection"); conn != response->headers.end() && conn->second == "keep-alive") {
			// save to keep alive
		} else {
			m_active_connections.erase(reusable_base);
		}

		store_in_cache_if_cachable(url, *response);

		if (response->status >= 300 && response->status < 400) {
			if (auto location_iter = response->headers.find("location"); location_iter != response->headers.end()) {
				auto location = location_iter->second;
				if (location.starts_with('/')) {
					url.path = location;
				} else {
					url = URL::create(location).value_or(URL::ABOUT_BLANK);
				}
				std::cerr << "Redirected to " << url << std::endl;
				continue;
			} else {
				std::cerr << "No `Location` header in redirect response " << url << std::endl;
				return std::nullopt;
			}
		}

		return response->body;
	}

	std::cout << "Too many redirects" << std::endl;
	return std::nullopt;
}
