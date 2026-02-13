#include "http.hpp"

HttpRequest::HttpRequest()
	: url(URL::ABOUT_BLANK)
	, method(HttpMethod::GET)
	, payload(std::nullopt)
{}

HttpRequest::HttpRequest(URL url)
	: url(url)
	, method(HttpMethod::GET)
	, payload(std::nullopt)
{}

HttpRequest::HttpRequest(URL u, HttpMethod m, std::optional<std::string> p)
	: url(u)
	, method(m)
	, payload(p)
{}

