#pragma once

#include <unordered_map>

#include "url.hpp"

enum class HttpMethod {
	GET,
	POST,
};

struct HttpRequest {
	URL url;
	HttpMethod method;
	std::optional<std::string> payload;

	HttpRequest(); // useless, this is for asio
	HttpRequest(URL url);
	HttpRequest(URL u, HttpMethod m, std::optional<std::string> p);
};

struct HttpResponse {
	int status;
	std::string version;
	std::string explanation;
	std::unordered_map<std::string, std::string> headers;
	std::string body;
};
