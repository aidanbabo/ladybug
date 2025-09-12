#include <curl/curl.h>
#include <curl/header.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include <cassert>
#include <cstring>
#include <cstdlib>

#include <algorithm>
#include <array>
#include <iostream>
#include <string>


size_t callback_func(char *ptr, size_t size, size_t nmemb, void *userdata) {
	std::string *s = (std::string *) userdata;
	size_t real_size = size * nmemb;
	s->append(ptr, real_size);
	return real_size;
}

struct URL {
	std::string scheme;
	std::string host;
	std::string path;

	URL(std::string_view url) {
		auto n = url.find("://");
		assert(std::string::npos != n);
		this->scheme = url.substr(0, n);
		constexpr std::array supported_protocols{"http", "https"};
		bool supported = std::find(supported_protocols.begin(), supported_protocols.end(), this->scheme) != supported_protocols.end();
		assert(supported);
		url = url.substr(n + 3);

		n = url.find("/");
		if (std::string::npos == n) {
			this->host = url;
			this->path = "/";
		} else {
			this->host = url.substr(0, n);
			this->path = std::string(url.substr(n));
		}
	}

	std::string request() const {
		CURL *curl = curl_easy_init();
		char error_buffer[CURL_ERROR_SIZE];
		error_buffer[0] = '\0';

		CURLcode res;
		res = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

		std::string url = this->scheme + "://" + this->host + this->path;
		res = curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		if (res != CURLE_OK) {
			if (strlen(error_buffer)) {
				std::cerr << error_buffer << std::endl;
			} else {
				std::cerr << curl_easy_strerror(res) << std::endl;
			}
			exit(1);
		}

		std::string response_data;
		res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback_func);
		assert(!res);
		res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
		assert(!res);

		res = curl_easy_perform(curl);
		assert(!res);

		CURLHcode resh;
		struct curl_header *hout;
		resh = curl_easy_header(curl, "transfer-encoding", 0, CURLH_HEADER, -1, &hout);
		assert(resh == CURLHE_MISSING);
		resh = curl_easy_header(curl, "content-encoding", 0, CURLH_HEADER, -1, &hout);
		assert(resh == CURLHE_MISSING);

		curl_easy_cleanup(curl);
		return response_data;
	}
};

void show(std::string_view body) {
	bool in_tag = false;
	for (char c : body) {
		if (c == '<') {
			in_tag = true;
		} else if (c == '>') {
			in_tag = false;
		} else if (!in_tag) {
			std::cout << c;
		}
	}
}

void load(URL url) {
	std::string body = url.request();
	show(body);
}

int main(int argc, char** argv) {
	assert(argc == 2);
	URL url = URL(argv[1]);
	load(url);
	return 0;
}
