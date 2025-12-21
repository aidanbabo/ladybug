#pragma once
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "url.hpp"

void network_init();

enum class HttpMethod {
	GET,
	POST,
};

struct HttpRequest {
	URL url;
	HttpMethod method;
	std::optional<std::string> payload;

	HttpRequest(URL url);
	HttpRequest(URL u, HttpMethod m, std::optional<std::string> p);
};

class HttpConnection;
struct CachedHttpResponse;

struct HttpResponse {
	int status;
	std::string version;
	std::string explanation;
	std::unordered_map<std::string, std::string> headers;
	std::string body;
};


// Some URLs don't need a ConnectionManager at all! Should we allow them to get their contents without access to a ConnectionManager?
// todo: Currently each tab gets their own manager for isolation, I don't think this is required tho and will speed things up.
class ConnectionManager {
	std::unordered_map<URL, std::unique_ptr<HttpConnection>> m_active_connections;
	std::unordered_map<URL, std::unique_ptr<CachedHttpResponse>> m_cached_responses;
	// website -> (cookie, params)
	std::unordered_map<std::string, std::pair<std::string, std::unordered_map<std::string, std::string>>> m_cookie_jar;

public:
	ConnectionManager();
	~ConnectionManager();
	std::optional<HttpResponse> request(HttpRequest const& request, std::optional<URL> referrer);
	void print_active_connections() const;
private:
	std::optional<std::string> load_file(URL url) const;
	std::optional<HttpResponse> load_http_or_from_cache(HttpRequest request, std::optional<URL> referrer);
	std::optional<HttpResponse> try_load_from_cache(HttpRequest const& request);
	void store_in_cache_if_cachable(HttpRequest const& request, HttpResponse const& response);
};
