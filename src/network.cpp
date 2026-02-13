#include <iostream>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <zlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <ctime>

#include <optional>

#include "network.hpp"
#include "network_thread.hpp"

void network_init() {
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
}

NetworkManager::NetworkManager()
	: m_io_context()
	, m_tasks(m_io_context)
	, m_network_thread(network_thread_entry, std::ref(m_io_context), std::ref(m_tasks))
{}


NetworkManager::~NetworkManager() {
	m_io_context.stop();

	if (m_network_thread.joinable())
		m_network_thread.join();
}

std::optional<HttpResponse> NetworkManager::block_for_request(HttpRequest const& request, std::optional<URL> referrer) {
	auto waiter = Waiter{};

	auto task = NetworkTask {
		.request = request,
		.referrer = referrer,
		.waiter = &waiter,
		.task_runner = nullptr,
		.after_network_task = std::nullopt,
	};

	assert(m_tasks.try_send(asio::error_code{}, std::move(task)));

	std::unique_lock lock(waiter.mutex);
	waiter.condvar.wait(lock, [&]{ return waiter.response.has_value(); });
	return waiter.response.value();
}

void NetworkManager::request_then_add_task(HttpRequest const& request, std::optional<URL> referrer, TaskRunner *task_runner, std::unique_ptr<AfterNetworkTask> aft) {
	auto task = NetworkTask {
		.request = request,
		.referrer = referrer,
		.waiter = nullptr,
		.task_runner = task_runner,
		.after_network_task = std::move(aft),
	};
	assert(m_tasks.try_send(asio::error_code{}, std::move(task)));
}
