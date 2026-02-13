#include <charconv>
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

#include <algorithm>
#include <iostream>
#include <memory>
#include <unordered_map>

#include "network_thread.hpp"
#include "utils.hpp"

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl.hpp>

using asio::ip::tcp;

struct Buffer {
	uint8_t *ptr;
	uint64_t size;

	Buffer() : Buffer(nullptr, 0) {}

	Buffer(uint8_t *p, uint64_t s)
		: ptr(p)
		, size(s)
	{}

	[[nodiscard]]
	std::string_view as_string_view() const {
		return std::string_view((char *)ptr, size);
	}

	[[nodiscard]]
	bool empty() const {
		return size == 0;
	}

	[[nodiscard]]
	Buffer advanced(uint64_t amount) const {
		assert(amount <= size);
		return Buffer(ptr + amount, size - amount);
	}

	[[nodiscard]]
	Buffer shrunk_to(uint64_t amount) const {
		assert(amount <= size);
		return Buffer(ptr, amount);
	}
};

// todo: This class has a lot of asserts around reading and validating data.
// Replacing these errors involves delving deeper into if they are:
// - Recoverable: The method can problem solve and produce a result anyway
// - Unrecoverable: The method must report failure to its caller
// - Fatal: The method must communicate that this HttpConnection
//   is no longer usable and should be destroyed (i.e. removed from the
//   reusable connection cache).
// This is currently a lot of work at this stage, and since we will also be handling
// POST request soon instead of just GET, I'm going to defer this refactor. It will
// undoubtedly complicated the code so I would rather add the necessary features and
// then refactor.
class HttpConnection {
	// todo: fix this silly hack
	std::optional<tcp::socket> m_socket;
	std::optional<asio::ssl::stream<tcp::socket>> m_ssl;

public:
	static asio::awaitable<std::optional<std::unique_ptr<HttpConnection>>> create(char const *host, uint16_t port, bool encrypt) {

		auto executor = co_await asio::this_coro::executor;

		std::string port_s = std::to_string(port);
		tcp::resolver resolver(executor);
		auto endpoints = co_await resolver.async_resolve(host, port_s, asio::use_awaitable);

		std::optional<tcp::socket> socket;
		std::optional<asio::ssl::stream<tcp::socket>> ssl_socket;
		if (encrypt) {
			asio::ssl::context ssl_ctx(asio::ssl::context::sslv23_client);
			ssl_socket = asio::ssl::stream<tcp::socket>(executor, ssl_ctx);
			co_await asio::async_connect(ssl_socket->next_layer(), endpoints, asio::use_awaitable);
			co_await ssl_socket->async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);
		} else {
			socket = tcp::socket(executor);
			co_await asio::async_connect(*socket, endpoints, asio::use_awaitable);
		}

		co_return std::make_unique<HttpConnection>(std::move(socket), std::move(ssl_socket));
	}

	HttpConnection(std::optional<tcp::socket> sock, std::optional<asio::ssl::stream<tcp::socket>> ssl)
		: m_socket(std::move(sock))
		, m_ssl(std::move(ssl))
	{}

	~HttpConnection() {
		// todo:
	}

	bool is_encrypted() const {
		return m_ssl.has_value();
	}

	// todo: reorganize parameters
	// This should actually not use the referrer but the 'top-level site'.
	asio::awaitable<std::optional<HttpResponse>> request(HttpRequest const& request, std::optional<URL> referrer, std::unordered_map<std::string, std::pair<std::string, std::unordered_map<std::string, std::string>>> const& cookie_jar) {
		std::string body{};
		if (request.method == HttpMethod::GET) {
			body += "GET " + request.url.path + " HTTP/1.1\r\n";
		} else if (request.method == HttpMethod::POST) {
			body += "POST " + request.url.path + " HTTP/1.1\r\n";
		} else {
			assert(false && "unreachable");
		}

		std::vector<std::pair<std::string_view, std::string_view>> request_headers;
		request_headers.push_back(std::make_pair("Host", std::string_view{request.url.host}));
		request_headers.push_back(std::make_pair("Connection", "keep-alive"));
		request_headers.push_back(std::make_pair("User-Agent", "ladybug 1.0"));
		request_headers.push_back(std::make_pair("Accept-Encoding", "gzip, deflate"));

		if (auto finder = cookie_jar.find(request.url.host); finder != cookie_jar.end()) {
			auto const& [cookie, params] = finder->second;
			bool allow_cookie = true;
			// todo: fixme: m_url is nullable just so we can support this branch. This stinks!
			if (referrer) {
				auto f = params.find("samesite");
				if (f != params.end() && f->second == "lax") {
					// This if has weird nesting in the book. I've learned not to mess with it as edits later will be more confusing.
					if (request.method != HttpMethod::GET) {
						allow_cookie = request.url.host == referrer->host;
					}
				}
			}
			if (allow_cookie) {
				request_headers.push_back(std::make_pair("Cookie", std::string_view{cookie}));
			}
		}

		if (request.method == HttpMethod::POST) {
			assert(request.payload != std::nullopt);
			request_headers.push_back(std::make_pair("Content-Length", std::to_string(request.payload->size())));
		}
		for (auto const& pair : request_headers) {
			body.append(pair.first);
			body.append(": ");
			body.append(pair.second);
			body.append("\r\n");
		}
		body.append("\r\n");
		if (request.payload) {
			body.append(*request.payload);
		}

		if (!co_await write(body)) {
			std::cerr << "Writing request failed" << std::endl;
			co_return std::nullopt;
		}

		int const buffer_size = 4096;
		uint8_t input_buffer_ptr[buffer_size];
		Buffer input_buffer { input_buffer_ptr, buffer_size };

		auto [response, remaining] = co_await parse_up_to_body(input_buffer);
		co_await parse_body(input_buffer, remaining, response);

		co_return response;
	}

	asio::awaitable<std::pair<HttpResponse, Buffer>> parse_up_to_body(Buffer input_buffer) {

		enum ParsingState {
			PARSING_STATUS_LINE,
			PARSING_HEADERS,
			PARSING_BODY,
		};

		std::string received;
		ParsingState parsing_state = PARSING_STATUS_LINE;
		HttpResponse response;


		Buffer buffer;
		do {
			buffer = co_await read(input_buffer);
			if (buffer.empty()) {
				break;
			}
			received.append(buffer.as_string_view());

			while (parsing_state != PARSING_BODY) {

				auto line_end = received.find("\r\n");
				if (line_end == std::string::npos) {
					break;
				}

				std::string_view line { std::string_view(received).substr(0, line_end) };
				if (parsing_state == PARSING_STATUS_LINE) {
					auto status = split(line, " ", 2);
					assert(status.size() == 3);

					response.version = status[0];
					// todo: no exceptions
					response.status = std::stoi(std::string{status[1]});
					response.explanation = status[2];

					parsing_state = PARSING_HEADERS;
				} else if (parsing_state == PARSING_HEADERS) {
					if (line == "") {
						parsing_state = PARSING_BODY;
					} else {
						auto header_split = split(line, ":", 1);
						assert(header_split.size() == 2);

						std::string header_name { header_split[0] };
						std::string_view header_value = header_split[1];
						make_lowercase(header_name);

						header_value = trim_whitespace(header_value);
						// todo: Handle multiple headers of the same value.
						// I don't think even some HTTP packages handle this correctly, so I don't think
						// it is a huge obstacle for browsing the web. That being said std::unordered_multimap
						// does exist and probably isn't that much of a headach
						response.headers.insert({header_name, std::string{header_value}});
					}
				} else {
					assert(false && "unreachable");
				}
				received = received.substr(line_end + 2);
			}
		} while (parsing_state != PARSING_BODY);

		// Here we want the bytes that are in the back of the buffer
		// and `received` contains only bytes for the body, which are also
		// in the input buffer.
		int data_offset = buffer.size - received.length();
		Buffer remaining { input_buffer.ptr + data_offset, received.length() };

		co_return std::make_pair(response, remaining);
	}

	asio::awaitable<void> parse_body(Buffer input_buffer, Buffer buffer, HttpResponse& response) {
		std::optional<uint64_t> total_content_length_opt;
		if (auto content_length_entry = response.headers.find("content-length"); content_length_entry != response.headers.end()) {
			// todo: no more exceptions!
			total_content_length_opt = std::stol(content_length_entry->second);
		}

		bool decompress = false;
		z_stream zstream;

		if (auto content_encoding = response.headers.find("content-encoding"); content_encoding != response.headers.end()) {
			if (content_encoding->second == "gzip") {
				decompress = true;
			} else if (content_encoding->second == "deflate") {
				decompress = true;
			}
		}

		bool chunked_transfer = false;
		if (auto transfer_encoding = response.headers.find("transfer-encoding"); transfer_encoding != response.headers.end()) {
			std::vector<std::string_view> s = split(transfer_encoding->second, ",");
			for (auto v : s) {
				std::string_view value = trim_whitespace(v);
				if (value == "gzip") {
					decompress = true;
				} else if (value == "deflate") {
					decompress = true;
				} else if (value == "chunked") {
					chunked_transfer = true;
				} else {
					// todo: error
					assert(false);
				}
			}
		}

		assert(total_content_length_opt || chunked_transfer);

		if (decompress) {
			std::cerr << "Uncompressing body" << std::endl;
			zstream.zalloc = Z_NULL;
			zstream.zfree = Z_NULL;
			zstream.opaque = Z_NULL;
			zstream.avail_in = 0;
			zstream.next_in = Z_NULL;
			// allow gzip and zlib wrappers
			int ret = inflateInit2(&zstream, 15 + 32);
			assert(ret == Z_OK);
		}

		uint64_t current_content_length = 0;

		if (chunked_transfer) {
			uint64_t size_of_current_chunk;
			// per chunk
			do {
				size_of_current_chunk = co_await parse_chunked_encoding_chunk_length(input_buffer, buffer);
				current_content_length = 0;
				// we break in the middle of this loop because of the data we start with
				do {
					uint64_t amount_to_read = std::min(size_of_current_chunk - current_content_length, buffer.size);
					Buffer to_decode { buffer.ptr, amount_to_read };
					decode_exact(to_decode, decompress, &zstream, response);

					if (amount_to_read == buffer.size) {
						buffer = co_await read(input_buffer);
					} else {
						buffer = buffer.advanced(amount_to_read);
					}

					current_content_length += amount_to_read;
				} while (current_content_length != size_of_current_chunk);

				if (buffer.size >= 2) {
					assert(buffer.ptr[0] == '\r');
					assert(buffer.ptr[1] == '\n');
					buffer = buffer.advanced(2);
				} else {
					assert(false && "Not handling split \\r\\n!");
				}

			} while (size_of_current_chunk != 0);
		} else {
			uint64_t total_content_length = *total_content_length_opt;
			// we break in the middle of this loop because of the data we start with
			for (;;) {
				uint64_t amount_to_read = std::min(total_content_length - current_content_length, buffer.size);
				Buffer to_decode { buffer.ptr, amount_to_read };
				decode_exact(to_decode, decompress, &zstream, response);

				current_content_length += amount_to_read;
				if (current_content_length == total_content_length) {
					break;
				}

				if (amount_to_read == buffer.size) {
					buffer = co_await read(input_buffer);

					// They may overestimate their Content-Length.
					if (buffer.empty()) {
						break;
					}
				} else {
					buffer = buffer.advanced(amount_to_read);
				}
			}
		}

		if (decompress) {
			inflateEnd(&zstream);
		}
	}

private:
	// mutable params with no warning spooks me :(
	asio::awaitable<uint64_t> parse_chunked_encoding_chunk_length(Buffer input_buffer, Buffer& curr) {
		uint64_t size_of_current_chunk = 0;
		for (;;) {
			uint8_t *newline = (uint8_t *)memmem(curr.ptr, curr.size, "\r\n", 2);
			if (newline == nullptr) {
				Buffer b { curr };
				bool ends_with_carraige_return = curr.ptr[curr.size - 1] == '\r';
				if (ends_with_carraige_return) {
					b.size--;
				}
				if (!b.empty()) {
					std::string_view s { b.as_string_view() };
					uint64_t adding;
					if (std::from_chars(s.data(), s.data() + s.size(), adding, 16).ec != std::errc{}) {
						// todo: is it really fatal?
						assert(false && "Fatal network error");
					}
					size_of_current_chunk = (size_of_current_chunk << 4 * b.size) + adding;
				}

				if (ends_with_carraige_return) {
					input_buffer.ptr[0] = '\r';
					Buffer offset = co_await read(input_buffer.advanced(1));
					curr = Buffer { input_buffer.ptr, offset.size + 1 };
				} else {
					curr = co_await read(input_buffer);
				}
			} else {
				Buffer b { curr.ptr, (uint64_t) (newline - curr.ptr) };
				if (!b.empty()) {
					std::string_view s { b.as_string_view() };
					uint64_t adding;
					if (std::from_chars(s.data(), s.data() + s.size(), adding, 16).ec != std::errc{}) {
						// todo: is it really fatal?
						assert(false && "Fatal network error");
					}
					size_of_current_chunk = (size_of_current_chunk << 4 * b.size) + adding;
				}

				curr = curr.advanced(b.size + 2);
				break;
			}
		}
		co_return size_of_current_chunk;
	}

	void decode_exact(Buffer buffer, bool decompress, z_stream *zstream, HttpResponse& response) {
		if (decompress) {
			int const output_buffer_size = 4096;
			uint8_t output_buffer[output_buffer_size];
			zstream->avail_in = buffer.size;
			zstream->next_in = buffer.ptr;
			do {
				zstream->avail_out = output_buffer_size;
				zstream->next_out = output_buffer;
				int ret = inflate(zstream, Z_NO_FLUSH);
				assert(ret != Z_STREAM_ERROR);
				switch (ret) {
				case Z_NEED_DICT:
					ret = Z_DATA_ERROR;
					[[fallthrough]];
				case Z_DATA_ERROR:
				case Z_MEM_ERROR:
					inflateEnd(zstream);
					// return ret; // from docs
					assert(false);
				}

				int have = output_buffer_size - zstream->avail_out;
				response.body.append((char *)output_buffer, have);
			} while (zstream->avail_out == 0);
		} else {
			response.body.append(buffer.as_string_view());
		}
	}

	asio::awaitable<bool> write(std::string data) {
		if (is_encrypted()) {
			co_await asio::async_write(*m_ssl, asio::buffer(data), asio::use_awaitable);
		} else {
			co_await asio::async_write(*m_socket, asio::buffer(data), asio::use_awaitable);
		}
		// todo: failure
		co_return true;
	}

	asio::awaitable<Buffer> read(Buffer buf) {
		if (is_encrypted()) {
			int bytes_received = co_await m_ssl->async_read_some(asio::buffer(buf.ptr, buf.size), asio::use_awaitable);
			assert(bytes_received != -1);
			co_return buf.shrunk_to(bytes_received);
		} else {
			int bytes_received = co_await m_socket->async_read_some(asio::buffer(buf.ptr, buf.size), asio::use_awaitable);
			assert(bytes_received != -1);
			co_return buf.shrunk_to(bytes_received);
		}
	}
};

struct CachedHttpResponse {
	HttpResponse response;
	std::optional<std::time_t> expires_at;
};

ConnectionManager::ConnectionManager(asio::io_context& io, asio::experimental::concurrent_channel<void(asio::error_code, NetworkTask)>& tq)
	: m_io_context(io)
	, m_task_queue(tq)
{}

ConnectionManager::~ConnectionManager() = default;

asio::awaitable<std::optional<HttpResponse>> ConnectionManager::request(HttpRequest const& request, std::optional<URL> referrer) {
	std::optional<HttpResponse> response;
	if (request.url.scheme == "file") {
		if (auto b = load_file(request.url)) {
			response = HttpResponse{};
			response->body = *b;
		}
	} else if (request.url.scheme == "data") {
		response = HttpResponse{};
		response->body = request.url.path;
	} else if (request.url.scheme == "http" || request.url.scheme == "https") {
		response = co_await load_http_or_from_cache(request, referrer);
	} else if (request.url.scheme == "about") {
		if (request.url.host == "blank") {
			response = HttpResponse{};
		} else {
			std::cerr << "Unsupported about url" << std::endl;
		}
	} else {
		assert(false && "unreachable");
	}

	if (request.url.view_source && response) {
		response->body = escape(response->body);
	}

	co_return response;
}

std::optional<std::string> ConnectionManager::load_file(URL url) const {
	return read_entire_file_to_string(url.path);
}

std::optional<HttpResponse> ConnectionManager::try_load_from_cache(HttpRequest const& request) {
	if (request.method != HttpMethod::GET) {
		return std::nullopt;
	}

	URL cachable_url = request.url.cachable_subsection();
	if (auto cached = m_cached_responses.find(cachable_url); cached != m_cached_responses.end()) {
		if (auto expires_at = cached->second->expires_at; expires_at && std::time(nullptr) >= *expires_at) {
			m_cached_responses.erase(cached);
		} else {
			std::cerr << "Using cached response for " << cachable_url << std::endl;
			return cached->second->response;
		}
	}
	return std::nullopt;
}

static std::pair<bool, std::optional<std::time_t>> response_is_cachable(HttpRequest const& request, HttpResponse const& response) {
	if (request.method != HttpMethod::GET) {
		return { false, std::nullopt };
	}
	// todo: add 301, 404, etc.
	if (response.status != 200) {
		return { false, std::nullopt };
	}

	auto ctrl_header = response.headers.find("cache-control");
	if (ctrl_header == response.headers.end()) {
		// fair game
		return { true, std::nullopt };
	}

	std::vector<std::string_view> directives = split(ctrl_header->second, ",");
	std::optional<uint64_t> max_age{};
	for (auto d : directives) {
		std::string_view directive = trim_whitespace(d);

		if (directive == "no-store") {
			return { false, std::nullopt };
		}

		std::string_view header_start = "max-age=";
		// todo: handle other values
		if (!directive.starts_with(header_start)) {
			return { false, std::nullopt };
		}

		assert(std::from_chars(directive.data() + header_start.size(), directive.data() + directive.size(), *max_age).ec == std::errc{});
	}

	if (max_age) {
		uint64_t age = 0;
		if (auto age_finder = response.headers.find("age"); age_finder != response.headers.end()) {
			age = std::stol(age_finder->second);
		}

		std::time_t expires_at = std::time(nullptr) + *max_age - age;
		return { true, expires_at };
	} else {
		return { true , std::nullopt };
	}

}

void ConnectionManager::store_in_cache_if_cachable(HttpRequest const& request, HttpResponse const& response) {
	auto [is_cachable, expires_at] = response_is_cachable(request, response);
	if (!is_cachable) {
		return;
	}

	URL cachable_url = request.url.cachable_subsection();
	auto cachable_response = std::make_unique<CachedHttpResponse>(CachedHttpResponse {
		.response = response,
		.expires_at = expires_at,
	});
	m_cached_responses.insert({cachable_url, std::move(cachable_response)});
}

asio::awaitable<std::optional<HttpResponse>> ConnectionManager::load_http_or_from_cache(HttpRequest request, std::optional<URL> referrer) {
	// Redirect loop.
	for (int i = 0; i < 10; i++) {
		if (auto content = try_load_from_cache(request)) {
			co_return *content;
		}

		URL reusable_base = request.url.reusable_connection_subsection();
		auto pair = m_active_connections.find(reusable_base);

		// I wish if statements were expressions so bad
		auto connection = co_await [&]() -> asio::awaitable<HttpConnection*> {
			if (pair == m_active_connections.end()) {
				std::cerr << "Creating new connection for " << reusable_base << " (" << request.url << ")" << std::endl;
				bool encrypted;
				if (request.url.scheme == "http") {
					encrypted = false;
				} else if (request.url.scheme == "https") {
					encrypted = true;
				} else {
					assert(false);
				}
				auto connection { co_await HttpConnection::create(request.url.host.c_str(), request.url.port, encrypted) };
				if (connection) {
					// todo: move to if else below in the network rework
					m_active_connections.insert({reusable_base, std::move(*connection)});
					HttpConnection *conn = m_active_connections[reusable_base].get();
					co_return conn;
				} else {
					std::cerr << "Connection to " << request.url << " failed" << std::endl;
					co_return nullptr;
				}
			} else {
				std::cerr << "Reusing connection for " << reusable_base << " (" << request.url << ")" << std::endl;
				co_return pair->second.get();
			}
		}();
		if (!connection) {
			co_return std::nullopt;
		}

		auto response = co_await connection->request(request, referrer, m_cookie_jar);
		if (!response) {
			co_return std::nullopt;
		}
		std::cerr << response->status << " to " << request.url << std::endl;
		if (auto conn = response->headers.find("connection"); conn != response->headers.end() && conn->second == "keep-alive") {
			// save to keep alive
		} else {
			m_active_connections.erase(reusable_base);
		}

		if (auto cookie_finder = response->headers.find("set-cookie"); cookie_finder != response->headers.end()) {
			auto& cookie { cookie_finder->second };
			std::unordered_map<std::string, std::string> params;
			if (cookie.find(';') != std::string::npos) {
				auto v = split(cookie, ";");
				cookie = v[0];
				for (size_t i = 1; i < v.size(); i++) {
					std::string key{}, value{};
					auto n = v[i].find(';');
					if (n == std::string::npos) {
						key = v[i];
						value = "true";
					} else {
						key = v[i].substr(0, n);
						value = v[i].substr(n + 1);
					}
					key = trim_whitespace(key);
					value = trim_whitespace(value);
					make_lowercase(key);
					make_lowercase(value);
					params[key] = value;
				}

			}
			m_cookie_jar[request.url.host] = std::make_pair(cookie, params);
		}

		store_in_cache_if_cachable(request, *response);

		if (response->status >= 300 && response->status < 400) {
			if (auto location_iter = response->headers.find("location"); location_iter != response->headers.end()) {
				auto location = location_iter->second;
				// todo: What to do about query parameters?
				// Should we add a field in the URL for them so they can be retained while the path changes?
				// What is actual correct redirect behavior?
				if (location.starts_with('/')) {
					request.url.path = location;
				} else {
					request.url = URL::create(location).value_or(URL::ABOUT_BLANK);
				}
				std::cerr << "Redirected to " << request.url << std::endl;
				continue;
			} else {
				std::cerr << "No `Location` header in redirect response " << request.url << std::endl;
				co_return std::nullopt;
			}
		}

		co_return response;
	}

	std::cout << "Too many redirects" << std::endl;
	co_return std::nullopt;
}

asio::awaitable<void> ConnectionManager::complete_task(NetworkTask t) {
	auto response = co_await request(t.request, t.referrer);
	if (t.waiter != nullptr) {
		{
			// no contention, safe to do in async world
			std::lock_guard lock(t.waiter->mutex);
			t.waiter->response = std::optional(std::move(response));
		}
		t.waiter->condvar.notify_all();
	}
	if (t.task_runner != nullptr) {
		assert(t.after_network_task.has_value());
		(*t.after_network_task)->response = response;
		std::unique_ptr<Task> task = std::move(*t.after_network_task);
		t.task_runner->schedule(std::move(task));
	}
}

asio::awaitable<void> ConnectionManager::wait_for_tasks() {
	while (true) {
		auto task = co_await m_task_queue.async_receive();
		// For now, wait for each task to be completed.
		// This code is not yet thread safe.
		co_await asio::co_spawn(m_io_context, complete_task(std::move(task)));
	}
}

void network_thread_entry(asio::io_context& io_context, asio::experimental::concurrent_channel<void(asio::error_code, NetworkTask)>& task_queue) {
	ConnectionManager cn(io_context, task_queue);
	asio::co_spawn(io_context, cn.wait_for_tasks(), asio::detached);
	io_context.run();
}

