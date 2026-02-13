#pragma once

#include <asio.hpp>
#include <asio/experimental/concurrent_channel.hpp>

#include <optional>

#include "task_runner.hpp"
#include "http.hpp"
#include "url.hpp"

void network_init();

struct Waiter {
	std::optional<std::optional<HttpResponse>> response;
	std::mutex mutex;
	std::condition_variable condvar;
};

struct NetworkTask {
	HttpRequest request;
	std::optional<URL> referrer;
	Waiter *waiter;
	TaskRunner *task_runner;
	std::optional<std::unique_ptr<AfterNetworkTask>> after_network_task;
};

class NetworkManager {
	asio::io_context m_io_context;
	asio::experimental::concurrent_channel<void(asio::error_code, NetworkTask)> m_tasks;
	std::thread m_network_thread;
public:
	NetworkManager();
	std::optional<HttpResponse> block_for_request(HttpRequest const& request, std::optional<URL> referrer);
	void request_then_add_task(HttpRequest const& request, std::optional<URL> referrer, TaskRunner *task_runner, std::unique_ptr<AfterNetworkTask> aft);
	~NetworkManager();
};
