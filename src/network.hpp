#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

void network_init();

struct URL {
	bool view_source;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string path;

	static std::optional<URL> create(std::string_view string);

// todo: i want these to be module-private
	bool operator==(const URL& other) const noexcept;
	URL reusable_connection_subsection() const;
	URL cachable_subsection() const;
	std::string to_string() const;
};

template <>
struct std::hash<URL> {
	std::size_t operator()(const URL& u) const noexcept;
};

struct HttpConnection;
struct HttpResponse;
struct CachedHttpResponse;

// Some URLs don't need a ConnectionManager at all! Should we allow them to get their contents without access to a ConnectionManager?
class ConnectionManager {
	// todo: default ctor and dtor? handle this (and the HttpConnections) correctly?
	std::unordered_map<URL, std::unique_ptr<HttpConnection>> m_active_connections;
	// todo: retest that this works
	std::unordered_map<URL, std::unique_ptr<CachedHttpResponse>> m_cached_responses;

public:
	ConnectionManager();
	~ConnectionManager();
	std::string request(URL url);
	void print_active_connections() const;
private:
	std::string load_file(URL url) const;
	std::string load_from_cache_or_fetch(URL url);
	void store_in_cache_if_cachable(URL url, HttpResponse response);
	std::string request_http(URL url);
};
