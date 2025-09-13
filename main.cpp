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
			std::cerr << gai_strerror(status) << std::endl;
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

	std::string request(std::string request) {
		if (is_encrypted()) {
			return request_with_tls(request);
		} else {
			return request_without_tls(request);
		}
	}

private:
	std::string request_with_tls(std::string request) const {

		// there may be handleable failures here for larger request sizes?
		int send_result = SSL_write(m_ssl, request.c_str(), request.size());
		if (send_result <= 0) {
			ERR_print_errors_fp(stderr);
			SSL_shutdown(m_ssl);
			SSL_free(m_ssl);
			SSL_CTX_free(m_ctx);
			close(m_socket_fd);
			assert(false);
		}

		std::string response;
		char buffer[1024];
		int bytes_received;
		// todo: remove infinite
		for (;;) {
			bytes_received = SSL_read(m_ssl, buffer, sizeof(buffer) - 1);
			if (bytes_received <= 0) {
				switch (SSL_get_error(m_ssl, bytes_received)) {
				case SSL_ERROR_ZERO_RETURN:
					goto after_read;
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
			if (bytes_received == 0) {
				break;
			}

			response.append(buffer, bytes_received);
		}

after_read:
		return response;
	}

	std::string request_without_tls(std::string request) const {
		int send_result = send(m_socket_fd, request.c_str(), request.size(), 0);
		assert(send_result != -1);

		std::string response;
		char buffer[1024];
		ssize_t bytes_received;
		// todo: remove infinite
		for (;;) {
			bytes_received = recv(m_socket_fd, buffer, sizeof(buffer) - 1, 0);
			assert(bytes_received != -1);
			if (bytes_received == 0) {
				break;
			}

			response.append(buffer, bytes_received);
		}
		return response;
	}
};

class URL {
	bool m_view_source = false;
	std::string m_scheme;
	std::string m_host;
	uint16_t m_port;
	std::string m_path;

public:
	URL(std::string_view url) {
		auto n = url.find(":");
		assert(n != std::string::npos);
		m_scheme = url.substr(0, n);

		if (m_scheme == "view-source") {
			m_view_source = true;
			url = url.substr(n + 1);

			n = url.find(":");
			assert(n != std::string::npos);
			m_scheme = url.substr(0, n);
		}

		constexpr std::array supported_protocols{"http", "https", "file", "data"};
		bool supported = std::find(supported_protocols.begin(), supported_protocols.end(), m_scheme) != supported_protocols.end();
		assert(supported);
		if (m_scheme == "data") {
			url = url.substr(n + 1);
		} else {
			assert(url.substr(n + 1, 2) == "//");
			url = url.substr(n + 3);
		}

		if (m_scheme == "data") {
			n = url.find(",");
			assert(n != std::string::npos);
			assert(url.substr(0, n) == "text/html");
			// not really what this is for storing...
			m_path = url.substr(n + 1);
			return;
		}

		n = url.find("/");
		if (n == std::string::npos) {
			m_host = url;
			m_path = "/";
		} else {
			m_host = url.substr(0, n);
			m_path = url.substr(n);
		}

		n = m_host.find(":");
		if (n != std::string::npos) {
			m_port = std::stoi(m_host.substr(n + 1));
			m_host = m_host.substr(0, n);
		} else if (m_scheme == "https") {
			m_port = 443;
		} else if (m_scheme == "http") {
			m_port = 80;
		} else if (m_scheme == "file") {
			m_port = 0;
		} else {
			assert(false && "unreachable");
		}

		if (m_scheme == "file") {
			assert(m_host == "");
			assert(m_port == 0);
		}
	}

	std::string request() const {

		std::string response;
		if (m_scheme == "file") {
			response = load_file();
		} else if (m_scheme == "data") {
			response = m_path;
		} else if (m_scheme == "http" || m_scheme == "https") {
			response = request_http();
		} else {
			assert(false);
		}

		if (m_view_source) {
			return escape(response);
		} else {
			return response;
		}
	}

private:
	std::string load_file() const {
		std::ifstream file(m_path);
		if (!file.is_open()) {
			std::cerr << "Invalid path" << std::endl;
			exit(1);
		}

		// huh, second paramater for what?
		std::string file_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();
		return file_content;
	}

	std::string request_http() const {

		bool encrypted;
		if (m_scheme == "http") {
			encrypted = false;
		} else if (m_scheme == "https") {
			encrypted = true;
		} else {
			assert(false);
		}
		HttpConnection connection = HttpConnection(m_host.c_str(), m_port, encrypted);

		std::string request = "GET " + m_path + " HTTP/1.1\r\n";

		std::vector<std::pair<std::string, std::string>> request_headers;
		request_headers.push_back(std::make_pair("Host", m_host));
		request_headers.push_back(std::make_pair("Connection", "close"));
		request_headers.push_back(std::make_pair("User-Agent", "ladybug 1.0"));
		for (auto pair : request_headers) {
			request.append(pair.first);
			request.append(": ");
			request.append(pair.second);
			request.append("\r\n");
		}
		request.append("\r\n");

		std::string response = connection.request(request);

		int status_line_end = response.find("\r\n");
		assert(status_line_end != std::string::npos);
		std::string status_line = response.substr(0, status_line_end);
		response = response.substr(status_line_end + 2);

		int version_end = status_line.find(" ");
		assert(version_end != std::string::npos);
		int http_status_end = status_line.find(" ", version_end + 1);
		assert(http_status_end != std::string::npos);

		std::string version = status_line.substr(0, version_end);
		std::string http_status = status_line.substr(version_end + 1, http_status_end);
		std::string explanation = status_line.substr(http_status_end + 1);

		std::unordered_map<std::string, std::string> response_headers;
		int max_headers = 250;
		int i;
		for (i = 0; i < max_headers; i++) {
			int line_end = response.find("\r\n");
			assert(line_end != std::string::npos);
			std::string line = response.substr(0, line_end);
			response = response.substr(line_end + 2);

			if (line == "") {
				break;
			}

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
		assert(i != max_headers);

		assert(response_headers.find("transfer-encoding") == response_headers.end());
		assert(response_headers.find("content-encoding") == response_headers.end());

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

void load(URL url) {
	std::string body = url.request();
	show(body);
}

int main(int argc, char** argv) {
	char const *url_string = (argc == 2) ? argv[1] : "file:///home/ababo/dev/browser/index.html";

	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	URL url = URL(url_string);
	load(url);
	return 0;
}
