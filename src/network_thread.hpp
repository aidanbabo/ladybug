#pragma once

#include "network.hpp"

void network_thread_entry(asio::io_context& io_context, asio::experimental::concurrent_channel<void(asio::error_code, NetworkTask)>& task_queue);

class HttpConnection;
struct CachedHttpResponse;

// Some URLs don't need a ConnectionManager at all! Should we allow them to get their contents without access to a ConnectionManager?
// todo: Currently each tab gets their own manager for isolation, I don't think this is required tho and will speed things up.
class ConnectionManager {
	asio::io_context& m_io_context;
	asio::experimental::concurrent_channel<void(asio::error_code, NetworkTask)>& m_task_queue;

	// introduce locks so threads cannot write to the same connection interleaving
	std::unordered_map<URL, std::unique_ptr<HttpConnection>> m_active_connections;
	std::unordered_map<URL, std::unique_ptr<CachedHttpResponse>> m_cached_responses;
	// website -> (cookie, params)
	std::unordered_map<std::string, std::pair<std::string, std::unordered_map<std::string, std::string>>> m_cookie_jar;

public:
	ConnectionManager(asio::io_context& io_context, asio::experimental::concurrent_channel<void(asio::error_code, NetworkTask)>& task_queue);
	~ConnectionManager();

	asio::awaitable<void> wait_for_tasks();

private:
	asio::awaitable<void> complete_task(NetworkTask t);
	asio::awaitable<std::optional<HttpResponse>> request(HttpRequest const& request, std::optional<URL> referrer);
	asio::awaitable<std::optional<HttpResponse>> load_http_or_from_cache(HttpRequest request, std::optional<URL> referrer);

	std::optional<std::string> load_file(URL url) const;
	std::optional<HttpResponse> try_load_from_cache(HttpRequest const& request);
	void store_in_cache_if_cachable(HttpRequest const& request, HttpResponse const& response);
};
