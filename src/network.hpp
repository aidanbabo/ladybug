#pragma once

#include <asio.hpp>
#include <asio/experimental/concurrent_channel.hpp>

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

struct Waiter {
	std::optional<std::optional<HttpResponse>> response;
	std::mutex mutex;
	std::condition_variable condvar;
};

struct NetworkTask {
	HttpRequest request;
	std::optional<URL> referrer;
	std::optional<Waiter*> waiter;
};

class NetworkManager {
	asio::io_context m_io_context;
	asio::experimental::concurrent_channel<void(asio::error_code, NetworkTask)> m_tasks;
	std::thread m_network_thread;
public:
	NetworkManager();
	std::optional<HttpResponse> block_for_request(HttpRequest const& request, std::optional<URL> referrer);
	~NetworkManager();
};
