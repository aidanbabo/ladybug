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
	};

	assert(m_tasks.try_send(asio::error_code{}, task));

	std::unique_lock lock(waiter.mutex);
	waiter.condvar.wait(lock, [&]{ return waiter.response.has_value(); });
	return waiter.response.value();
}
