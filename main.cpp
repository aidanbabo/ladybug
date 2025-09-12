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


struct URL {
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string path;

	URL(std::string_view url) {
		auto n = url.find(":");
		assert(n != std::string::npos);
		this->scheme = url.substr(0, n);
		constexpr std::array supported_protocols{"http", "https", "file", "data"};
		bool supported = std::find(supported_protocols.begin(), supported_protocols.end(), this->scheme) != supported_protocols.end();
		assert(supported);
		if (this->scheme == "data") {
			url = url.substr(n + 1);
		} else {
			assert(url.substr(n + 1, 2) == "//");
			url = url.substr(n + 3);
		}

		if (this->scheme == "data") {
			n = url.find(",");
			assert(n != std::string::npos);
			assert(url.substr(0, n) == "text/html");
			// not really what this is for storing...
			this->path = url.substr(n + 1);
			return;
		}

		n = url.find("/");
		if (n == std::string::npos) {
			this->host = url;
			this->path = "/";
		} else {
			this->host = url.substr(0, n);
			this->path = url.substr(n);
		}

		n = this->host.find(":");
		if (n != std::string::npos) {
			this->port = std::stoi(this->host.substr(n + 1));
			this->host = this->host.substr(0, n);
		} else if (this->scheme == "https") {
			this->port = 443;
		} else if (this->scheme == "http") {
			this->port = 80;
		} else if (this->scheme == "file") {
			this->port = 0;
		} else {
			assert(false && "unreachable");
		}

		if (this->scheme == "file") {
			assert(this->host == "");
			assert(this->port == 0);
		}
	}

	std::string request() const {

		if (this->scheme == "file") {
			return this->load_file();
		}

		if (this->scheme == "data") {
			return this->path;
		}

		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		std::string port = std::to_string(this->port);

		addrinfo *res;
		int status = getaddrinfo(this->host.c_str(), port.c_str(), &hints, &res);
		if (status != 0) {
			std::cerr << gai_strerror(status) << std::endl;
			assert(false);
		}

		int socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (socket_fd == -1) {
			perror("socket");
			freeaddrinfo(res);
			assert(false);
		}

		int connection_result = connect(socket_fd, res->ai_addr, res->ai_addrlen);
		freeaddrinfo(res);
		if (connection_result == -1) {
			perror("connect");
			close(socket_fd);
			assert(false);
		}

		std::string request = "GET " + this->path + " HTTP/1.1\r\n";

		std::vector<std::pair<std::string, std::string>> request_headers;
		request_headers.push_back(std::make_pair("Host", this->host));
		request_headers.push_back(std::make_pair("Connection", "close"));
		request_headers.push_back(std::make_pair("User-Agent", "ladybug 1.0"));
		for (auto pair : request_headers) {
			request.append(pair.first);
			request.append(": ");
			request.append(pair.second);
			request.append("\r\n");
		}
		request.append("\r\n");

		std::string response;
		// todo: unified "connection" object to unify tls and non-tls
		// ala Python
		if (this->scheme == "http") {
			response = this->request_http(socket_fd, request);
		} else if (this->scheme == "https") {
			response = this->request_https(socket_fd, request);
		} else {
			assert(false);
		}
		close(socket_fd);

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

private:
	std::string load_file() const {
		std::ifstream file(this->path);
		if (!file.is_open()) {
			std::cerr << "Invalid path" << std::endl;
			exit(1);
		}

		// huh, second paramater for what?
		std::string file_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();
		return file_content;
	}

	std::string request_http(int socket_fd, std::string request) const {
		int send_result = send(socket_fd, request.c_str(), request.size(), 0);
		assert(send_result != -1);

		std::string response;
		char buffer[1024];
		ssize_t bytes_received;
		// todo: remove infinite
		for (;;) {
			bytes_received = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
			assert(bytes_received != -1);
			if (bytes_received == 0) {
				break;
			}

			response.append(buffer, bytes_received);
		}

		return response;
	}

	std::string request_https(int socket_fd, std::string request) const {

		const SSL_METHOD *method = TLS_client_method();
		SSL_CTX *ctx = SSL_CTX_new(method);
		if (!ctx) {
			ERR_print_errors_fp(stderr);
			close(socket_fd);
			assert(false);
		}

		SSL *ssl = SSL_new(ctx);
		SSL_set_fd(ssl, socket_fd);

		if (SSL_connect(ssl) <= 0) {
			ERR_print_errors_fp(stderr);
			SSL_free(ssl);
			SSL_CTX_free(ctx);
			close(socket_fd);
			assert(false);
		}

		int send_result = SSL_write(ssl, request.c_str(), request.size());
		if (send_result <= 0) {
			ERR_print_errors_fp(stderr);
			SSL_free(ssl);
			SSL_CTX_free(ctx);
			close(socket_fd);
			assert(false);
		}

		std::string response;
		char buffer[1024];
		ssize_t bytes_received;
		// todo: remove infinite
		for (;;) {
			bytes_received = SSL_read(ssl, buffer, sizeof(buffer) - 1);
			assert(bytes_received != -1);
			if (bytes_received == 0) {
				break;
			}

			response.append(buffer, bytes_received);
		}

		SSL_shutdown(ssl);
		SSL_free(ssl);
		SSL_CTX_free(ctx);

		return response;
	}
};

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
				assert(i + 1 < body.length());
				char c2 = body[i];
				char c3 = body[i + 1];
				if (c2 == 'l' && c3 == 't') {
					std::cout << '<';
					i += 2;
				} else if (c2 == 'g' && c3 == 't') {
					std::cout << '>';
					i += 2;
				} else {
					// is this right?
					std::cout << '&';
				}
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
