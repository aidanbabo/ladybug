#include <openssl/ssl.h>
#include <openssl/err.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <cstdlib>

#include <algorithm>
#include <array>
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

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

	std::string request(URL url) {
		std::string request = "GET " + url.path + " HTTP/1.1\r\n";

		std::vector<std::pair<std::string, std::string>> request_headers;
		request_headers.push_back(std::make_pair("Host", url.host));
		request_headers.push_back(std::make_pair("Connection", "keep-alive"));
		request_headers.push_back(std::make_pair("User-Agent", "ladybug 1.0"));
		for (auto pair : request_headers) {
			request.append(pair.first);
			request.append(": ");
			request.append(pair.second);
			request.append("\r\n");
		}
		request.append("\r\n");
		write(request);

		std::unordered_map<std::string, std::string> response_headers;
		std::string response;

		enum ParsingState {
			PARSING_STATUS_LINE,
			PARSING_HEADERS,
			PARSING_BODY,
		};

		ParsingState parsing_state = PARSING_STATUS_LINE;

		for (;;) {
			if (parsing_state == PARSING_BODY) {
				auto content_length_entry = response_headers.find("content-length");
				if (content_length_entry != response_headers.end() && response.length() == std::stoi(content_length_entry->second)) {
					break;
				}
			}


			char buffer[1024];
			int bytes_received = read(buffer, sizeof(buffer) - 1);
			response.append(buffer, bytes_received);
			if (bytes_received == 0) {
				break;
			}
			if (parsing_state == PARSING_BODY) {
				continue;
			}

			int line_end = response.find("\r\n");
			if (line_end == std::string::npos) {
				continue;
			}

			std::string line = response.substr(0, line_end);
			response = response.substr(line_end + 2);
			if (parsing_state == PARSING_STATUS_LINE) {
				int version_end = line.find(" ");
				assert(version_end != std::string::npos);
				int http_status_end = line.find(" ", version_end + 1);
				assert(http_status_end != std::string::npos);

				std::string version = line.substr(0, version_end);
				std::string http_status = line.substr(version_end + 1, http_status_end);
				std::string explanation = line.substr(http_status_end + 1);
				parsing_state = PARSING_HEADERS;
			} else if (parsing_state == PARSING_HEADERS) {
				if (line == "") {
					assert(response_headers.find("transfer-encoding") == response_headers.end());
					assert(response_headers.find("content-encoding") == response_headers.end());
					parsing_state = PARSING_BODY;
				} else {
					int colon = line.find(":");
					assert(colon != std::string::npos);

					std::string header = line.substr(0, colon);
					std::transform(header.begin(), header.end(), header.begin(), ::tolower);

					std::string value = line.substr(colon + 1);
					char const *whitespace = " \t\n\r\f\v";
					value.erase(value.find_last_not_of(whitespace) + 1);
					value.erase(0, value.find_first_not_of(whitespace));

					response_headers.insert({header, value});
				}
			} else {
				assert(false);
			}
		}

		return response;
	}

private:
	void write(std::string data) {
		if (is_encrypted()) {
			// there may be handleable failures here for larger request sizes?
			int send_result = SSL_write(m_ssl, data.c_str(), data.size());
			if (send_result <= 0) {
				ERR_print_errors_fp(stderr);
				SSL_shutdown(m_ssl);
				SSL_free(m_ssl);
				SSL_CTX_free(m_ctx);
				close(m_socket_fd);
				assert(false);
			}
		} else {
			int send_result = send(m_socket_fd, data.c_str(), data.size(), 0);
			if (send_result == -1) {
				perror("send");
				assert(false);
			}
		}
	}

	int read(char *buffer, int length) const {
		if (is_encrypted()) {
			int bytes_received = SSL_read(m_ssl, buffer, sizeof(buffer) - 1);
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
			int bytes_received = recv(m_socket_fd, buffer, sizeof(buffer) - 1, 0);
			assert(bytes_received != -1);
			return bytes_received;
		}
	}
};

class ConnectionManager {
	// default ctor and dtor? handle this (and the HttpConnections) correctly?
	std::unordered_map<std::string, HttpConnection *> m_active_connections;

public:
	std::string request(URL url) {
		std::string response;
		if (url.scheme == "file") {
			response = load_file(url);
		} else if (url.scheme == "data") {
			response = url.path;
		} else if (url.scheme == "http" || url.scheme == "https") {
			response = request_http(url);
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

	std::string request_http(URL url) {
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

		std::string response = connection->request(url);
		return response;
	}
};

int unescape_sequence(std::string_view body, int i) {
	assert(i + 2 < body.length());
	char c2 = body[i];
	char c3 = body[i + 1];
	char c4 = body[i + 2];
	if (c2 == 'l' && c3 == 't' && c4 == ';') {
		std::cout << '<';
		i += 3;
	} else if (c2 == 'g' && c3 == 't' && c4 == ';') {
		std::cout << '>';
		i += 3;
	} else {
		assert(i + 3 < body.length());
		char c5 = body[i + 3];
		if (c2 == 'a' && c3 == 'm' && c4 == 'p' && c5 == ';') {
			std::cout << '&';
			i += 4;
		} else {
			assert(false);
		}
	}
	return i;
}

void show(std::string_view body) {
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
				i = unescape_sequence(body, i);
			} else { 
				std::cout << c;
			}
		}
	}
}

void load(ConnectionManager& cm, URL url) {
	std::string body = cm.request(url);
	show(body);
}

int main(int argc, char** argv) {
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	auto connection_manager = ConnectionManager();
	if (argc == 1) {
		URL url = URL("file:///home/ababo/dev/browser/index.html");
		load(connection_manager, url);
	} else {
		for (int i = 1; i < argc; i++) {
			URL url = URL(argv[i]);
			load(connection_manager, url);
		}
	}
	return 0;
}
