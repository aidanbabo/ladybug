#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

void network_init();
std::string url_encode(std::string_view s);

enum class HttpMethod {
	GET,
	POST,
};

struct URL {
	bool view_source;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string path;
	std::string fragment;

	static URL ABOUT_BLANK;

	static std::optional<URL> create(std::string_view string);
	std::optional<URL> resolve(std::string_view url) const;
	bool equal_disregarding_fragment(URL const& other) const;


	bool operator==(const URL& other) const noexcept;
// todo: i want these to be module-private
	URL reusable_connection_subsection() const;
	URL cachable_subsection() const;
};

std::ostream& operator<<(std::ostream& os, URL const& url);

template <>
struct std::hash<URL> {
	std::size_t operator()(const URL& u) const noexcept;
};

struct HttpRequest {
	URL url;
	HttpMethod method;
	std::optional<std::string> payload;

	HttpRequest(URL url);
	HttpRequest(URL u, HttpMethod m, std::optional<std::string> p);
};

class HttpConnection;
struct HttpResponse;
struct CachedHttpResponse;

// Some URLs don't need a ConnectionManager at all! Should we allow them to get their contents without access to a ConnectionManager?
// todo: Currently each tab gets their own manager for isolation, I don't think this is required tho and will speed things up.
class ConnectionManager {
	std::unordered_map<URL, std::unique_ptr<HttpConnection>> m_active_connections;
	std::unordered_map<URL, std::unique_ptr<CachedHttpResponse>> m_cached_responses;

public:
	ConnectionManager();
	~ConnectionManager();
	std::optional<std::string> request(HttpRequest const& request);
	void print_active_connections() const;
private:
	std::optional<std::string> load_file(URL url) const;
	std::optional<std::string> load_http_or_from_cache(HttpRequest request);
	std::optional<std::string> try_load_from_cache(HttpRequest const& request);
	void store_in_cache_if_cachable(HttpRequest const& request, HttpResponse const& response);
};
