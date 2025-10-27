// todo: Exercise 2-5: Emoji

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// Skia
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"

#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_directory.h"
#include "include/core/SkFontMetrics.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <zlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

int const INITIAL_WIDTH  = 800;
int const INITIAL_HEIGHT = 600;
int const SCROLL_STEP = 100;
int const HSTEP = 13;
int const VSTEP = 18;

std::string escape(std::string source) {
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

std::string trim_whitespace(std::string s) {
	char const *whitespace = " \t\n\r\f\v";
	s.erase(s.find_last_not_of(whitespace) + 1);
	s.erase(0, s.find_first_not_of(whitespace));
	return s;
}

std::vector<std::string> split(std::string s, std::string const& delimiter, int nsplits = -1) {
	std::vector<std::string> items;
	size_t start = 0;
	for (;;) {
		size_t end_pos = s.find(delimiter, start);
		if (end_pos == std::string::npos || (int) items.size() == nsplits) {
			std::string item = s.substr(start);
			items.push_back(item);
			return items;
		}

		std::string item = s.substr(start, end_pos - start);
		items.push_back(item);
		start = end_pos + delimiter.length();
	}
}

std::vector<std::string> split_on_any(std::string s, std::string const& delimiters, int nsplits = -1) {
	std::vector<std::string> items;
	size_t start = 0;
	for (;;) {
		size_t end_pos = s.find_first_of(delimiters, start);
		if (end_pos == std::string::npos || (int) items.size() == nsplits) {
			std::string item = s.substr(start);
			items.push_back(item);
			return items;
		}

		std::string item = s.substr(start, end_pos - start);
		items.push_back(item);
		start = end_pos + 1;
	}
}

struct URL {
	bool view_source;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string path;

	static std::optional<URL> create(std::string_view string) {
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

	bool operator==(const URL& other) const noexcept {
		return view_source == other.view_source && scheme == other.scheme && host == other.host && port == other.port && path == other.path;
	}

	// todo: special hash and eq impls for this?
	URL reusable_connection_subsection() const {
		return URL {
			.view_source = false,
			.scheme = scheme,
			.host = host,
			.port = port,
			.path = "",
		};
	}

	// todo: special hash and eq impls for this?
	URL cachable_subsection() const {
		return URL {
			.view_source = false,
			.scheme = scheme,
			.host = host,
			.port = port,
			.path = path,
		};
	}

	std::string to_string() const {
		std::string source = (view_source) ? "view-source:" : "";
		return source + scheme + "://" + host + ":" + std::to_string(port) + path;
	}
};

// from Boost
void combine_hash(size_t &seed, size_t value) {
	seed ^= (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

template <>
struct std::hash<URL> {
	std::size_t operator()(const URL& u) const noexcept {
		size_t seed = 0;
		combine_hash(seed, std::hash<size_t>{}(u.view_source));
		combine_hash(seed, std::hash<std::string>{}(u.scheme));
		combine_hash(seed, std::hash<std::string>{}(u.host));
		combine_hash(seed, std::hash<uint16_t>{}(u.port));
		combine_hash(seed, std::hash<std::string>{}(u.path));
		return seed;
	}
};

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

		std::vector<std::pair<std::string, std::string>> request_headers;
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
					response.status = std::stoi(status[1]);
					response.explanation = status[2];

					parsing_state = PARSING_HEADERS;
				} else if (parsing_state == PARSING_HEADERS) {
					if (line == "") {
						parsing_state = PARSING_BODY;
						break;
					} else {
						auto header_split = split(line, ":", 1);
						assert(header_split.size() == 2);

						std::string header_name = header_split[0];
						std::string header_value = header_split[1];
						std::transform(header_name.begin(), header_name.end(), header_name.begin(), ::tolower);

						header_value = trim_whitespace(header_value);
						response.headers.insert({header_name, header_value});
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
			auto s = split(transfer_encoding->second, ",");
			for (auto v : s) {
				std::string value = trim_whitespace(v);
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

// Some URLs don't need a ConnectionManager at all! Should we allow them to get their contents without access to a ConnectionManager?
class ConnectionManager {
	// todo: default ctor and dtor? handle this (and the HttpConnections) correctly?
	std::unordered_map<URL, std::unique_ptr<HttpConnection>> m_active_connections;
	// todo: retest that this works
	std::unordered_map<URL, CachedHttpResponse> m_cached_responses;

public:
	std::string request(URL url) {
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

	void print_active_connections() const {
		for (auto const& p : m_active_connections) {
			std::cerr << p.first.to_string() << std::endl;
		}
	}

private:
	std::string load_file(URL url) const {
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

	std::string load_from_cache_or_fetch(URL url) {
		// todo: custom hash impl? is this even worth?
		URL cachable_url = url.cachable_subsection();
		if (auto cached = m_cached_responses.find(cachable_url); cached != m_cached_responses.end()) {
			std::time_t now = std::time(nullptr);
			if (now >= cached->second.expires_at) {
				m_cached_responses.erase(cached);
			} else {
				std::cerr << "Using cached response for " << cachable_url.to_string() << std::endl;
				return cached->second.response.body;
			}
		}
		// todo: move caching to after this step?
		return request_http(url);
	}

	void store_in_cache_if_cachable(URL url, HttpResponse response) {
		// todo: add 301, 404, etc.
		if (response.status != 200) {
			return;
		}

		auto ctrl_header = response.headers.find("cache-control");
		if (ctrl_header == response.headers.end()) {
			return;
		}

		std::vector<std::string> directives = split(ctrl_header->second, ",");
		for (std::string d : directives) {
			std::string directive = trim_whitespace(d);

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
			uint64_t max_age = std::stol(directive.substr(header_start.length()));

			uint64_t age = 0;
			if (auto age_finder = response.headers.find("age"); age_finder != response.headers.end()) {
				age = std::stol(age_finder->second);
			}

			std::time_t expires_at = std::time(nullptr) + max_age - age;

			URL cachable_url = url.cachable_subsection();
			CachedHttpResponse cachable_response = {
				.response = response,
				.expires_at = expires_at,
			};
			m_cached_responses.insert({cachable_url, cachable_response});
		}
	}

	std::string request_http(URL url) {
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
};

// lowkey hate implicit mutable references, but pairs also suck...
char unescape_sequence(std::string_view body, size_t &i) {
	assert(i + 2 < body.length());
	char c2 = body[i];
	char c3 = body[i + 1];
	char c4 = body[i + 2];
	if (c2 == 'l' && c3 == 't' && c4 == ';') {
		i += 3;
		return '<';
	} else if (c2 == 'g' && c3 == 't' && c4 == ';') {
		i += 3;
		return '>';
	} else {
		assert(i + 3 < body.length());
		char c5 = body[i + 3];
		if (c2 == 'a' && c3 == 'm' && c4 == 'p' && c5 == ';') {
			i += 4;
			return '&';
		}
	}
	i += 1;
	return '&';
}

enum class TokenTag {
	Text,
	Tag,
};

struct Token {
	TokenTag tag;
	std::string data;
};

// todo: handle nesting tags...
std::vector<Token> lex(std::string_view body) {
	std::vector<Token> out;
	std::string buffer;
	size_t i = 0;
	bool in_tag = false;
	while (i < body.length()) {
		char c = body[i++];
		if (c == '<') {
			in_tag = true;
			if (!buffer.empty()) {
				out.push_back(Token {
					.tag = TokenTag::Text,
					.data = buffer,
				});
				buffer = "";
			}
		} else if (c == '>') {
			in_tag = false;
			if (!buffer.empty()) {
				out.push_back(Token {
					.tag = TokenTag::Tag,
					.data = buffer,
				});
				buffer = "";
			}
		} else if (!in_tag && c == '&') {
			buffer.push_back(unescape_sequence(body, i));
		} else { 
			buffer.push_back(c);
		}
	}

	if (!in_tag && !buffer.empty()) {
		out.push_back(Token {
			.tag = TokenTag::Text,
			.data = buffer,
		});
	}
	return out;
}

struct StringPosition {
	float x, y;
	// todo: support unicode
	std::string string;
	// todo: is copying this around cheap?
	SkFont font;
};

struct ComputedLayout {
	std::vector<StringPosition> display_list;
	float must_render_up_to_y;
};

struct FontType {
	sk_sp<SkTypeface> normal;
	sk_sp<SkTypeface> bold;
	sk_sp<SkTypeface> italic;
	sk_sp<SkTypeface> bold_italic;
};

struct FontInfo {
	size_t size;
	bool bold;
	bool italic;

	bool operator==(const FontInfo& other) const noexcept {
		return size == other.size && bold == other.bold && italic == other.italic;
	}
};

template <>
struct std::hash<FontInfo> {
	size_t operator()(const FontInfo& f) const noexcept {
		size_t seed = 0;
		combine_hash(seed, std::hash<size_t>{}(f.size));
		combine_hash(seed, std::hash<bool>{}(f.bold));
		combine_hash(seed, std::hash<bool>{}(f.italic));
		return seed;
	}
};

class FontCache {
	std::unordered_map<FontInfo, SkFont> m_fonts;
	FontType m_font_type;

public:
	FontCache(FontType ty) : m_font_type(ty) {}

	SkFont& get_font(size_t size, bool bold, bool italic) {
		auto info = FontInfo {
			.size = size,
			.bold = bold,
			.italic = italic,
		};
		if (auto f = m_fonts.find(info); f != m_fonts.end()) {
			return f->second;
		}

		sk_sp<SkTypeface> typeface = [&] {
			if (!bold && !italic) {
				return m_font_type.normal;
			} else if (bold && !italic) {
				return m_font_type.bold;
			} else if (!bold && italic) {
				return m_font_type.italic;
			} else if (bold && italic) {
				return m_font_type.bold_italic;
			} else {
				assert(false && "unreachable");
			}
		}();
		SkFont font(typeface, size);
		m_fonts.insert({info, font});
		return m_fonts[info];
	}
};

// this approach is odd...
class Layout {
	std::vector<StringPosition> m_display_list;
	float m_cursor_x = HSTEP;
	float m_cursor_y = VSTEP;
	float m_must_render_up_to_y = VSTEP;
	bool m_is_bold = false;
	bool m_is_italic = false;
	bool m_in_title = false;
	int m_size = 12;
	// positions will have useless y coordinates
	std::vector<StringPosition> m_line;

	int m_width;
	bool m_right_align;

public:
	Layout(std::vector<Token> tokens, FontCache &font_cache, int width, bool right_align) {
		m_width = width;
		m_right_align = right_align;

		for (Token const& tok : tokens) {
			token(tok, font_cache);
		}
	
		flush();
	}

	ComputedLayout computed() const {
		return {
			.display_list = std::move(m_display_list),
			.must_render_up_to_y = m_must_render_up_to_y,
		};
	}

private:
	void token(Token const& tok, FontCache &font_cache) {
		if (tok.tag == TokenTag::Text) {
			SkFont &font = font_cache.get_font(m_size, m_is_bold, m_is_italic);

			auto words = split_on_any(tok.data, " \r\n\t");
			for (auto const &w : words) {
				auto w2 = trim_whitespace(w);
				if (w2 != "") {
					word(w2, font);
				}
			}
		} else if (tok.tag == TokenTag::Tag) {
			if (tok.data == "i") {
				m_is_italic = true;
			} else if (tok.data == "/i") {
				m_is_italic = false;
			} else if (tok.data == "b") {
				m_is_bold = true;
			} else if (tok.data == "/b") {
				m_is_bold = false;
			} else if (tok.data == "small") {
				m_size -= 2;
			} else if (tok.data == "/small") {
				m_size += 2;
			} else if (tok.data == "big") {
				m_size += 4;
			} else if (tok.data == "/big") {
				m_size -= 4;
			} else if (tok.data == "br") {
				flush();
			} else if (tok.data == "/p") {
				flush();
				m_cursor_y += VSTEP;
			// todo: remove. this is non-standard
			} else if (tok.data == "h1 class=\"title\"") {
				flush();
				m_in_title = true;
			} else if (tok.data == "/h1") {
				flush();
				m_in_title = false;
			} else {
				// do nothing
			}
		} else {
			assert(false && "unreachable");
		}
	}

	void word(std::string const &word, SkFont &font) {
		// todo: other text encodings
		auto w = font.measureText(word.c_str(), word.size(), SkTextEncoding::kUTF8);

		if (m_cursor_x + w >= m_width - HSTEP) {
			flush();
		}

		auto pos = StringPosition {
			.x = m_cursor_x,
			.y = -1, // filled in during `flush`
			.string = word,
			.font = font,
		};
		m_line.push_back(pos);

		// todo: other text encodings
		m_cursor_x += w + font.measureText(" ", 1, SkTextEncoding::kUTF8);
	}

	void flush() {
		if (m_line.empty()) {
			return;
		}

		// todo: cool function for this?
		std::vector<SkFontMetrics> metrics;
		for (auto const& pos : m_line) {
			SkFontMetrics m;
			pos.font.getMetrics(&m);
			metrics.push_back(m);
		}
		// todo: cool function for this!
		// ascent in skia is typically a negative number, but for Tk it's positive...
		float max_ascent = -100000;
		for (auto const& m : metrics) {
			if (-m.fAscent > max_ascent) {
				max_ascent = -m.fAscent;
			}
		}
		// todo: metrics.fLeading
		float baseline = m_cursor_y + max_ascent * 1.25;

		// todo: zip?
		for (size_t i = 0; i < m_line.size(); i++) {
			// apparently, Skia draws text from the baseline, not from the NW
			m_line[i].y = baseline;// + metrics[i].fAscent;
		}

		if (m_in_title || m_right_align) {
			auto const& w = m_line[m_line.size() - 1];
			// todo: put in StringPosition?
			auto word_width = w.font.measureText(w.string.c_str(), w.string.size(), SkTextEncoding::kUTF8);
			float right_side_gap = (float) m_width - w.x - word_width - (float) HSTEP;

			float change = (m_in_title) ? right_side_gap / 2 : right_side_gap;
			for (auto &pos : m_line) {
				pos.x += change;
			}
		}

		for (auto pos : m_line) {
			m_display_list.push_back(std::move(pos));
		}

		// todo: cool function for this!
		float max_descent = -100000;
		for (auto const& m : metrics) {
			if (m.fDescent > max_descent) {
				max_descent = m.fDescent;
			}
		}

		// todo: metrics.fLeading
		m_cursor_y = baseline + max_descent * 1.25;
		m_cursor_x = HSTEP;
		m_line = {};

		if (m_cursor_y + VSTEP > m_must_render_up_to_y) {
			m_must_render_up_to_y = m_cursor_y + VSTEP;
		}
	}
};

void initialize_texture(SDL_Renderer *renderer, int width, int height, SDL_Texture *&texture, sk_sp<SkSurface> &root_surface, SkImageInfo &info) {
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, width, height);

	info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
	SkSurfaceProps surface_props;
	size_t row_bytes = width * 4;
	root_surface = SkSurfaces::Raster(info, row_bytes, &surface_props);
	assert(root_surface);
}


class Browser {
	SDL_Window *m_window;
	SDL_Renderer *m_renderer;
	SDL_Texture *m_texture;
	sk_sp<SkSurface> m_root_surface;
	SkImageInfo m_surface_info;
	sk_sp<SkFontMgr> m_font_mgr;
	std::vector<Token> m_tokens;
	ComputedLayout m_layout;
	FontCache m_font_cache;

	int m_scroll = 0;
	int m_width = INITIAL_WIDTH;
	int m_height = INITIAL_HEIGHT;
	// todo: make a cli arg! find a library for this!
	bool m_right_align = false;

public:
	static std::optional<Browser> create() {
		// todo: change to OpenGL
		SDL_Window *window = SDL_CreateWindow("Ladybug", INITIAL_WIDTH, INITIAL_HEIGHT, SDL_WINDOW_RESIZABLE);

		if (window == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create window: %s\n", SDL_GetError());
			return std::nullopt;
		}

		SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);

		SDL_Texture *texture;
		SkImageInfo info;
		sk_sp<SkSurface> root_surface;
		initialize_texture(renderer, INITIAL_WIDTH, INITIAL_HEIGHT, texture, root_surface, info);

		// this doesn't ever want to seem to work so full paths it is
		sk_sp<SkFontMgr> font_mgr = SkFontMgr_New_Custom_Directory("/home/ababo/dev/browser/fonts");
		assert(font_mgr);
		sk_sp<SkTypeface> normal      = font_mgr->makeFromFile("/home/ababo/dev/browser/fonts/Times New Roman.ttf");
		sk_sp<SkTypeface> bold        = font_mgr->makeFromFile("/home/ababo/dev/browser/fonts/Times New Roman Bold.ttf");
		sk_sp<SkTypeface> italic      = font_mgr->makeFromFile("/home/ababo/dev/browser/fonts/Times New Roman Italic.ttf");
		sk_sp<SkTypeface> bold_italic = font_mgr->makeFromFile("/home/ababo/dev/browser/fonts/Times New Roman Bold Italic.ttf");
		assert(normal);
		assert(bold);
		assert(italic);
		assert(bold_italic);

		FontType times_new_roman = FontType {
			.normal = normal,
			.bold = bold,
			.italic = italic,
			.bold_italic = bold_italic,
		};

		FontCache font_cache = FontCache(times_new_roman);

		return Browser(window, renderer, texture, root_surface, info, font_mgr, font_cache);
	}

	void load(ConnectionManager& cm, URL url) {
		std::string body = cm.request(url);
		m_tokens = lex(body);
		m_layout = Layout(m_tokens, m_font_cache, m_width, m_right_align).computed();
		draw();
	}
	
	void draw() {
		SkPaint paint;
		paint.setColor(SK_ColorBLACK);

		auto canvas = m_root_surface->getCanvas();
		canvas->clear(SK_ColorWHITE);

		// content
		for (auto cpos : m_layout.display_list) {
			// todo: adding VSTEP is a crutch? idk why it doesn't work without it
			if (cpos.y > m_scroll + m_height + VSTEP) continue;
			if (cpos.y + VSTEP < m_scroll) continue;
			canvas->drawString(cpos.string.c_str(), cpos.x, cpos.y - m_scroll, cpos.font, paint);
		}

		// scrollbar
		if (m_height < m_layout.must_render_up_to_y) {
			float scrollbar_ratio = (float) m_height / (float) m_layout.must_render_up_to_y;
			float scrollbar_size = scrollbar_ratio * (float) m_height;
			float scrollbar_start = scrollbar_ratio * (float) m_scroll;
			paint.setColor(SK_ColorBLUE);
			canvas->drawRect(SkRect::MakeLTRB(m_width - HSTEP, scrollbar_start, m_width, scrollbar_start + scrollbar_size), paint);
		}

		sk_sp<SkImage> image = m_root_surface->makeImageSnapshot();

		// todo: change to m_row_bytes
		size_t row_bytes = m_width * 4;

		std::vector<uint8_t> pixels(m_height * m_width * 4);
		if (!image->readPixels(m_surface_info, pixels.data(), row_bytes, 0, 0)) {
			// todo: error
			assert(false);
		}

		SDL_UpdateTexture(m_texture, nullptr, pixels.data(), row_bytes);

		SDL_RenderClear(m_renderer);
		SDL_RenderTexture(m_renderer, m_texture, nullptr, nullptr);
		SDL_RenderPresent(m_renderer);
	}

	void resize(int new_width, int new_height) {
		m_width = new_width;
		m_height = new_height;
		SDL_DestroyTexture(m_texture);

		initialize_texture(m_renderer, m_width, m_height, m_texture, m_root_surface, m_surface_info);

		m_layout = Layout(m_tokens, m_font_cache, m_width, m_right_align).computed();
		draw();
	}

	void scroll_up() {
		m_scroll -= SCROLL_STEP;
		if (m_scroll < 0) {
			m_scroll = 0;
		}
		draw();
	}

	void scroll_down() {
		m_scroll += SCROLL_STEP;
		int max_scroll = m_layout.must_render_up_to_y - m_height;
		if (max_scroll < 0) {
			max_scroll = 0;
		}
		if (m_scroll > max_scroll) {
			m_scroll = max_scroll;
		}
		draw();
	}

	void destroy() {
		SDL_DestroyTexture(m_texture);
		SDL_DestroyRenderer(m_renderer);
		SDL_DestroyWindow(m_window);
	}

private:
	Browser(
		SDL_Window *window,
		SDL_Renderer *renderer,
		SDL_Texture *texture,
		sk_sp<SkSurface> root_surface,
		SkImageInfo surface_info,
		sk_sp<SkFontMgr> font_mgr,
		FontCache font_cache
	)
		: m_window(window)
		, m_renderer(renderer)
		, m_texture(texture)
		, m_root_surface(root_surface)
		, m_surface_info(surface_info)
		, m_font_mgr(font_mgr)
		, m_layout()
		, m_font_cache(font_cache)
	{}
};

int main(int argc, char** argv) {
	bool done = false;

	SDL_Init(SDL_INIT_VIDEO);

	auto b = Browser::create();
	if (!b.has_value()) {
		SDL_Quit();
		return 1;
	}
	Browser browser = b.value();

	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	auto connection_manager = ConnectionManager();
	URL url = [&] {
		// todo: how to make value_or lazy
		if (argc == 1) {
			return URL::create("file:///home/ababo/dev/browser/index.html").value_or(URL::create("about:blank").value());
		} else if (argc == 2) {
			return URL::create(argv[1]).value_or(URL::create("about:blank").value());
		} else {
			assert(false && "Invalid arguments");
		}
	}();

	browser.load(connection_manager, url);

	while (!done) {
		SDL_Event event;

		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				done = true;
			} else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				int width = event.window.data1;
				int height = event.window.data2;
				browser.resize(width, height);
			} else if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_DOWN) {
					browser.scroll_down();
				} else if (event.key.key == SDLK_UP) {
					browser.scroll_up();
				}
			} else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
				// todo: smooth scrolling
				if (event.wheel.y > 0) {
					browser.scroll_up();
				} else if (event.wheel.y < 0) {
					browser.scroll_down();
				}
			}
		}

		SDL_Delay(16);
	}

	browser.destroy();

	SDL_Quit();

	return 0;
}
