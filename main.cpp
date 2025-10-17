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

int const WIDTH  = 800;
int const HEIGHT = 600;
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
		if (end_pos == std::string::npos || items.size() == nsplits) {
			std::string item = s.substr(start);
			items.push_back(item);
			return items;
		}

		std::string item = s.substr(start, end_pos - start);
		items.push_back(item);
		start = end_pos + delimiter.length();
	}
}

struct URL {
	bool view_source = false;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string path;

	URL(std::string_view url) {
		auto n = url.find(":");
		assert(n != std::string::npos);
		scheme = url.substr(0, n);

		if (scheme == "view-source") {
			view_source = true;
			url = url.substr(n + 1);

			n = url.find(":");
			assert(n != std::string::npos);
			scheme = url.substr(0, n);
		}

		constexpr std::array supported_protocols{"http", "https", "file", "data"};
		bool supported = std::find(supported_protocols.begin(), supported_protocols.end(), scheme) != supported_protocols.end();
		assert(supported);
		if (scheme == "data") {
			url = url.substr(n + 1);
		} else {
			assert(url.substr(n + 1, 2) == "//");
			url = url.substr(n + 3);
		}

		if (scheme == "data") {
			n = url.find(",");
			assert(n != std::string::npos);
			assert(url.substr(0, n) == "text/html");
			// not really what this is for storing...
			path = url.substr(n + 1);
			return;
		}

		n = url.find("/");
		if (n == std::string::npos) {
			host = url;
			path = "/";
		} else {
			host = url.substr(0, n);
			path = url.substr(n);
		}

		n = host.find(":");
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
	}

	URL(std::string scheme, std::string host, uint16_t port) 
		: scheme(scheme)
		, host(host)
		, port(port)
		, path("/")
	{}

	std::string base() const {
		return scheme + "://" + host + ":" + std::to_string(port);
	}

	std::string cachable_subsection() const {
		return scheme + "://" + host + ":" + std::to_string(port) + path;
	}

	std::string to_string() const {
		return scheme + "://" + host + ":" + std::to_string(port) + path;
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

				int line_end = received.find("\r\n");
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

class ConnectionManager {
	// default ctor and dtor? handle this (and the HttpConnections) correctly?
	std::unordered_map<std::string, HttpConnection *> m_active_connections;
	// GAHHHH MAKING THINGS Hash SUCKS
	std::unordered_map<std::string, CachedHttpResponse> m_cached_responses;

public:
	std::string request(URL url) {
		std::string response;
		if (url.scheme == "file") {
			response = load_file(url);
		} else if (url.scheme == "data") {
			response = url.path;
		} else if (url.scheme == "http" || url.scheme == "https") {
			response = load_from_cache_or_fetch(url);
		} else {
			assert(false);
		}

		if (url.view_source) {
			return escape(response);
		} else {
			return response;
		}
	}

	void print_active_connections() const {
		for (auto p : m_active_connections) {
			std::cerr << p.first << ": " << p.second << std::endl;
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
		std::string cachable_url = url.cachable_subsection();
		if (auto cached = m_cached_responses.find(cachable_url); cached != m_cached_responses.end()) {
			std::time_t now = std::time(nullptr);
			if (now >= cached->second.expires_at) {
				m_cached_responses.erase(cached);
			} else {
				std::cerr << "Using cached response for " << cachable_url << std::endl;
				return cached->second.response.body;
			}
		}
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

			std::string cachable_url = url.cachable_subsection();
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
			std::string base = url.base();
			auto pair = m_active_connections.find(base);

			// I wish if statements were expressions so bad
			HttpConnection *connection = [&] {
				if (pair == m_active_connections.end()) {
					std::cerr << "Creating new connection for " << base << std::endl;
					bool encrypted;
					if (url.scheme == "http") {
						encrypted = false;
					} else if (url.scheme == "https") {
						encrypted = true;
					} else {
						assert(false);
					}
					HttpConnection *connection = new HttpConnection(url.host.c_str(), url.port, encrypted);
					m_active_connections.insert({base, connection});
					HttpConnection *conn = m_active_connections[base];
					return conn;
				} else {
					std::cerr << "Reusing connection for " << base << std::endl;
					return pair->second;
				}
			}();

			HttpResponse response = connection->request(url);
			if (response.status >= 300 && response.status < 400) {
				std::string location = response.headers["location"];
				if (location.rfind("/", 0) == 0) { // starts_with
					url.path = location;
				} else {
					url = URL(location);
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
char unescape_sequence(std::string_view body, int &i) {
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

std::string lex(std::string_view body) {
	std::string out;
	int i = 0;
	bool in_tag = false;
	while (i < body.length()) {
		char c = body[i++];
		if (c == '<') {
			in_tag = true;
		} else if (c == '>') {
			in_tag = false;
		} else if (!in_tag) {
			if (c == '&') {
				out.push_back(unescape_sequence(body, i));
			} else { 
				out.push_back(c);
			}
		}
	}
	return out;
}

struct CharacterPosition {
	int x, y;
	// todo: support unicode
	char c;
};

std::vector<CharacterPosition> layout(std::string text) {
	int cursor_x = HSTEP;
	int cursor_y = VSTEP;
	std::vector<CharacterPosition> display_list;
	// todo: use skia native tools?
	// todo: unicode
	// todo: newlines
	for (char c : text) {
		auto pos = (CharacterPosition){
			.x = cursor_x,
			.y = cursor_y,
			.c = c,
		};
		display_list.push_back(pos);

		cursor_x += HSTEP;

		if (cursor_x >= WIDTH - HSTEP) {
			cursor_y += VSTEP;
			cursor_x = HSTEP;
		}
	}
	return display_list;
}


class Browser {
	SDL_Window *m_window;
	SDL_Renderer *m_renderer;
	SDL_Texture *m_texture;
	sk_sp<SkSurface> m_root_surface;
	SkImageInfo m_surface_info;
	sk_sp<SkFontMgr> m_font_mgr;
	std::vector<CharacterPosition> m_display_list;
	int m_scroll = 0;

public:
	static std::optional<Browser> create() {
		// todo: change to OpenGL
		SDL_Window *window = SDL_CreateWindow("Ladybug", WIDTH, HEIGHT, 0);

		if (window == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create window: %s\n", SDL_GetError());
			return std::nullopt;
		}
		SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
		SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

		SkImageInfo info = SkImageInfo::Make(WIDTH, HEIGHT, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
		SkSurfaceProps surface_props;
		size_t row_bytes = WIDTH * 4;
		sk_sp<SkSurface> root_surface = SkSurfaces::Raster(info, row_bytes, &surface_props);
		assert(root_surface);

		sk_sp<SkFontMgr> font_mgr = SkFontMgr_New_Custom_Directory("/home/ababo/dev/browser/fonts");
		assert(font_mgr);

		return Browser(window, renderer, texture, root_surface, info, font_mgr);
	}

	void load(ConnectionManager& cm, URL url) {
		std::string body = cm.request(url);
		std::string text = lex(body);
		m_display_list = layout(text);
		draw();
	}
	
	void draw() {
		sk_sp<SkTypeface> typeface = m_font_mgr->matchFamilyStyle("Arial", SkFontStyle());
		assert(typeface);
		SkFont font(typeface, 12);

		SkPaint paint;
		paint.setColor(SK_ColorBLACK);

		auto canvas = m_root_surface->getCanvas();
		canvas->clear(SK_ColorWHITE);

		for (auto cpos : m_display_list) {
			if (cpos.y > m_scroll + HEIGHT) continue;
			if (cpos.y + VSTEP < m_scroll) continue;
			std::string s(1, cpos.c);
			canvas->drawString(s.c_str(), cpos.x, cpos.y - m_scroll, font, paint);
		}

		sk_sp<SkImage> image = m_root_surface->makeImageSnapshot();

		// todo: change to m_row_bytes
		size_t row_bytes = WIDTH * 4;

		std::vector<uint8_t> pixels(HEIGHT * WIDTH * 4);
		if (!image->readPixels(m_surface_info, pixels.data(), row_bytes, 0, 0)) {
			// todo: error
			assert(false);
		}

		SDL_UpdateTexture(m_texture, nullptr, pixels.data(), row_bytes);

		SDL_RenderClear(m_renderer);
		SDL_RenderTexture(m_renderer, m_texture, nullptr, nullptr);
		SDL_RenderPresent(m_renderer);
	}

	void scroll_down() {
		m_scroll += SCROLL_STEP;
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
		sk_sp<SkFontMgr> font_mgr
	)
		: m_window(window)
		, m_renderer(renderer)
		, m_texture(texture)
		, m_root_surface(root_surface)
		, m_surface_info(surface_info)
		, m_font_mgr(font_mgr)
		, m_display_list()
	{}
};

int main(int argc, char** argv) {
	SDL_Window *window;
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
		if (argc == 1) {
			return URL("file:///home/ababo/dev/browser/index.html");
		} else if (argc == 2) {
			return URL(argv[1]);
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
			}

			if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_DOWN) {
					browser.scroll_down();
				}
			}
		}
	}

	browser.destroy();

	SDL_Quit();

	return 0;
}
