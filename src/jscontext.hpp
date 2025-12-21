#pragma once
#include <duktape.h>
#include <SDL3/SDL_timer.h>

#include <string_view>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "url.hpp"
#include "parsers.hpp"

class Tab;

struct EventDispatchResult {
	bool do_default;
	bool propagate;
};

class JSContext {
	duk_context *m_interp;
	Tab& m_tab;
	std::unordered_map<std::shared_ptr<Node>, int> m_node_to_handle;
	// is this not just an array?
	std::unordered_map<int, std::shared_ptr<Node>> m_handle_to_node;
	std::unordered_set<int> m_timers;

	static duk_ret_t duk_console_log(duk_context *ctx);
	static duk_ret_t duk_document_query_selector_all(duk_context *ctx);
	static duk_ret_t duk_document_create_element(duk_context *ctx);
	static duk_ret_t duk_node_get_attribute(duk_context *ctx);
	static duk_ret_t duk_node_set_inner_html(duk_context *ctx);
	static duk_ret_t duk_node_children(duk_context *ctx);
	static duk_ret_t duk_node_append_child(duk_context *ctx);
	static duk_ret_t duk_node_insert_before(duk_context *ctx);
	static duk_ret_t duk_node_remove_child(duk_context *ctx);
	static duk_ret_t duk_xml_http_request_send(duk_context *ctx);
	static duk_ret_t duk_internal_set_timeout(duk_context *ctx);
	static Uint32 set_timeout_callback(void *userdata, SDL_TimerID timerID, Uint32 interval);

	void inject_console(duk_context *ctx);
	void inject_document(duk_context *ctx);
	void inject_internal(duk_context *ctx);
	void extend_node(duk_context *ctx);
	void extend_xml_http_request(duk_context *ctx);

	int get_handle(std::shared_ptr<Node> const& n);
public:
	JSContext(Tab& tab);
	~JSContext();

	void run(std::string_view code, std::optional<URL> script_url);

	[[nodiscard]]
	EventDispatchResult dispatch_event(std::string_view type, std::shared_ptr<Element> el);
};
