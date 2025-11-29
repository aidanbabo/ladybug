#include <cassert>
#include <format>
#include <iostream>

#include "ui.hpp"
#include "parsers.hpp"
#include "utils.hpp"

#include "jscontext.hpp"

static JSContext *get_js_context(duk_context *ctx) {
	duk_push_heap_stash(ctx);
	duk_get_prop_string(ctx, -1, "jscontext");
	auto jsctx = static_cast<JSContext *>(duk_get_pointer(ctx, -1));
	duk_pop_2(ctx);
	return jsctx;
}

static void remove_child(Node& parent, std::shared_ptr<Node> child) {
	for (size_t i = 0; i < parent.children.size(); i++) {
		if (parent.children[i] == child) {
			parent.children.erase(parent.children.begin() + i);
			return;
		}
	}
	assert(false && "Unreachable");
}

duk_ret_t JSContext::duk_console_log(duk_context *ctx) {
	std::cout << "console.log from js: ";

	duk_idx_t nargs = duk_get_top(ctx);
	for (auto i = 0; i < nargs; i++) {
		char const *s = duk_safe_to_string(ctx, i);
		std::cout << (s ? s : "undefined");
		if (i + 1 < nargs) {
			std::cout << " ";
		}
	}
	std::cout << std::endl;
	return 0;
}

void JSContext::inject_console(duk_context *ctx) {
	// push global
	duk_push_global_object(ctx);
	// create console
	duk_push_object(ctx);
	// push our function
	duk_push_c_function(ctx, duk_console_log, DUK_VARARGS);
	// put it under log in console
	duk_put_prop_string(ctx, -2, "log");
	// put it under console in global object
	duk_put_prop_string(ctx, -2, "console");
	// pop global
	duk_pop(ctx);
}

duk_ret_t JSContext::duk_document_query_selector_all(duk_context *ctx) {
	char const* string = duk_get_string(ctx, -1);
	if (!string) {
		return DUK_RET_TYPE_ERROR;
	}
	auto selector = CSSParser(std::string{string}).selector();
	if (!selector) {
		return DUK_RET_ERROR;
	}

	auto jsctx = get_js_context(ctx);

	std::vector<std::shared_ptr<Node>> nodes;
	html_tree_to_list(jsctx->m_tab.m_nodes, nodes);
	nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](auto const& n) {
		return !(*selector)->matches(*n);
	}), nodes.end());

	duk_push_array(ctx);
	for (size_t i = 0; i < nodes.size(); i++) {
		auto const& n = nodes[i];
		int handle = jsctx->get_handle(n);
		assert(duk_get_global_string(ctx, "Node"));
		duk_push_int(ctx, handle);
		assert(duk_pnew(ctx, 1) == 0);

		duk_put_prop_string(ctx, -2, std::to_string(i).c_str());
	}

	return 1;
}

duk_ret_t JSContext::duk_document_create_element(duk_context *ctx) {
	char const* tag = duk_get_string(ctx, -1);
	if (!tag) {
		return DUK_RET_TYPE_ERROR;
	}
	auto jsctx = get_js_context(ctx);
	auto element = std::make_shared<Element>(std::weak_ptr<Node>{}, std::string{tag}, std::unordered_map<std::string, std::string>{});
	int handle = jsctx->get_handle(element);

	assert(duk_get_global_string(ctx, "Node"));
	duk_push_int(ctx, handle);
	assert(duk_pnew(ctx, 1) == 0);

	return 1;
}

void JSContext::inject_document(duk_context *ctx) {
	// push global
	duk_push_global_object(ctx);
	// create document
	duk_push_object(ctx);

	duk_push_c_function(ctx, duk_document_query_selector_all, 1);
	duk_put_prop_string(ctx, -2, "querySelectorAll");

	duk_push_c_function(ctx, duk_document_create_element, 1);
	duk_put_prop_string(ctx, -2, "createElement");

	// put it under console in global object
	duk_put_prop_string(ctx, -2, "document");
	// pop global
	duk_pop(ctx);
}

duk_ret_t JSContext::duk_node_get_attribute(duk_context *ctx) {
	// Get args.
	char const* attr = duk_get_string(ctx, -1);
	duk_push_this(ctx);
	assert(duk_get_prop_string(ctx, -1, "handle"));
	int handle = duk_get_int_default(ctx, -1, -1);
	if (handle == -1) {
		return DUK_RET_ERROR;
	}
	duk_pop_2(ctx);

	auto jsctx = get_js_context(ctx);

	// Do call.
	auto n = jsctx->m_handle_to_node.find(handle);
	if (n == jsctx->m_handle_to_node.end() || n->second->type != NodeType::Element) {
		return DUK_RET_ERROR;
	}
	auto el = static_cast<Element const&>(*n->second);
	if (auto a = el.attributes.find(attr); a != el.attributes.end()) {
		duk_push_string(ctx, a->second.c_str());
	} else {
		duk_push_null(ctx);
	}
	return 1;
}

duk_ret_t JSContext::duk_node_set_inner_html(duk_context *ctx) {
	// Get args.
	char const* s = duk_get_string(ctx, -1);
	duk_push_this(ctx);
	assert(duk_get_prop_string(ctx, -1, "handle"));
	int handle = duk_get_int_default(ctx, -1, -1);
	if (handle == -1) {
		return DUK_RET_ERROR;
	}
	duk_pop_2(ctx);

	auto jsctx = get_js_context(ctx);

	// Do call.
	auto doc = HTMLParser(std::string{"<html><body>"} + s + "</body></html>").parse();
	auto new_nodes = doc->children[0]->children;
	// todo: there should be away for there to be optionals :(
	auto elt = jsctx->m_handle_to_node.at(handle);
	elt->children = new_nodes;
	for (auto& child : elt->children) {
		child->parent = elt;
	}
	jsctx->m_tab.render();
	return 0;
}

duk_ret_t JSContext::duk_node_children(duk_context *ctx) {
	// Get args.
	duk_push_this(ctx);
	assert(duk_get_prop_string(ctx, -1, "handle"));
	int handle = duk_get_int_default(ctx, -1, -1);
	if (handle == -1) {
		return DUK_RET_ERROR;
	}
	duk_pop_2(ctx);

	auto jsctx = get_js_context(ctx);

	auto elt = jsctx->m_handle_to_node.at(handle);
	duk_push_array(ctx);
	size_t i = 0;
	for (auto const& n : elt->children) {
		if (n->type != NodeType::Element) {
			continue;
		}
		int child_handle = jsctx->get_handle(n);
		assert(duk_get_global_string(ctx, "Node"));
		duk_push_int(ctx, child_handle);
		assert(duk_pnew(ctx, 1) == 0);

		duk_put_prop_string(ctx, -2, std::to_string(i).c_str());
		i++;
	}

	return 1;
}

duk_ret_t JSContext::duk_node_append_child(duk_context *ctx) {
	duk_get_prop_string(ctx, -1, "handle");
	int to_add_handle = duk_get_int_default(ctx, -1, -1);
	if (to_add_handle == -1) {
		return DUK_RET_ERROR;
	}
	duk_pop(ctx);

	duk_push_this(ctx);
	duk_get_prop_string(ctx, -1, "handle");
	int parent_handle = duk_get_int_default(ctx, -1, -1);
	if (parent_handle == -1) {
		return DUK_RET_ERROR;
	}
	duk_pop_2(ctx);

	auto jsctx = get_js_context(ctx);
	auto to_add = jsctx->m_handle_to_node.at(to_add_handle);
	auto parent = jsctx->m_handle_to_node.at(parent_handle);

	if (auto old_parent = to_add->parent.lock(); old_parent != nullptr) {
		remove_child(*old_parent, to_add);
	}

	parent->children.push_back(to_add);
	to_add->parent = parent;

	duk_dup_top(ctx);
	return 1;
}

duk_ret_t JSContext::duk_node_insert_before(duk_context *ctx) {
	duk_get_prop_string(ctx, -2, "handle");
	int to_add_handle = duk_get_int_default(ctx, -1, -1);
	if (to_add_handle == -1) {
		return DUK_RET_ERROR;
	}
	duk_pop(ctx);

	duk_get_prop_string(ctx, -1, "handle");
	int before_this_node_handle = duk_get_int_default(ctx, -1, -1);
	if (before_this_node_handle == -1) {
		return DUK_RET_ERROR;
	}
	duk_pop(ctx);

	duk_push_this(ctx);
	duk_get_prop_string(ctx, -1, "handle");
	int parent_handle = duk_get_int_default(ctx, -1, -1);
	if (parent_handle == -1) {
		return DUK_RET_ERROR;
	}
	duk_pop_2(ctx);

	auto jsctx = get_js_context(ctx);
	auto to_add = jsctx->m_handle_to_node.at(to_add_handle);
	auto parent = jsctx->m_handle_to_node.at(parent_handle);
	auto before_this_node = jsctx->m_handle_to_node.at(before_this_node_handle);

	auto before_location = std::find(parent->children.begin(), parent->children.end(), before_this_node);
	if (before_location == parent->children.end()) {
		return DUK_RET_ERROR;
	}

	if (auto old_parent = to_add->parent.lock(); old_parent != nullptr) {
		remove_child(*old_parent, to_add);
	}

	parent->children.insert(before_location, to_add);
	to_add->parent = parent;

	duk_dup(ctx, -2);
	return 1;
}

duk_ret_t JSContext::duk_node_remove_child(duk_context *ctx) {
	duk_get_prop_string(ctx, -1, "handle");
	int to_remove_handle = duk_get_int_default(ctx, -1, -1);
	if (to_remove_handle == -1) {
		return DUK_RET_ERROR;
	}
	duk_pop(ctx);

	duk_push_this(ctx);
	duk_get_prop_string(ctx, -1, "handle");
	int parent_handle = duk_get_int_default(ctx, -1, -1);
	if (parent_handle == -1) {
		return DUK_RET_ERROR;
	}
	duk_pop_2(ctx);

	auto jsctx = get_js_context(ctx);
	auto to_remove = jsctx->m_handle_to_node.at(to_remove_handle);
	auto parent = jsctx->m_handle_to_node.at(parent_handle);

	if (auto old_parent = to_remove->parent.lock(); old_parent != parent) {
		return DUK_RET_ERROR;
	} else {
		remove_child(*parent, to_remove);
	}

	to_remove->parent = std::weak_ptr<Node>{};

	duk_dup_top(ctx);
	return 1;
}

void JSContext::extend_node(duk_context *ctx) {
	assert(duk_get_global_string(ctx, "Node"));
	assert(duk_get_prop_string(ctx, -1, "prototype"));
	duk_push_c_function(ctx, duk_node_get_attribute, 1);
	duk_put_prop_string(ctx, -2, "getAttribute");

	// we still have [node, proto]

	duk_push_string(ctx, "innerHTML");
	duk_push_c_function(ctx, duk_node_set_inner_html, 1);

	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_SET_ENUMERABLE);

	duk_push_string(ctx, "children");
	duk_push_c_function(ctx, duk_node_children, 1);

	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_SET_ENUMERABLE);

	// we still have [node, proto]
	
	duk_push_c_function(ctx, duk_node_append_child, 1);
	duk_put_prop_string(ctx, -2, "appendChild");

	duk_push_c_function(ctx, duk_node_insert_before, 2);
	duk_put_prop_string(ctx, -2, "insertBefore");

	duk_push_c_function(ctx, duk_node_remove_child, 1);
	duk_put_prop_string(ctx, -2, "removeChild");

	// we still have [node, proto]

	duk_pop_2(ctx);
}

int JSContext::get_handle(std::shared_ptr<Node> const& n) {
	int handle;
	if (auto e = m_node_to_handle.find(n); e != m_node_to_handle.end()) {
		handle = e->second;
	} else {
		handle = m_node_to_handle.size();
		m_node_to_handle[n] = handle;
		m_handle_to_node[handle] = n;
	}
	return handle;
}

JSContext::JSContext(Tab& tab) : m_tab(tab) {
	m_interp = duk_create_heap_default();
	if (!m_interp) {
		std::cerr << "Unable to create JS context" << std::endl;
		assert(false);
	}
	// make ourselves accessible from inside JS code
	duk_push_heap_stash(m_interp);
	duk_push_pointer(m_interp, this);
	duk_put_prop_string(m_interp, -2, "jscontext");
	duk_pop(m_interp);

	auto f = read_entire_file_to_string("runtime_support/runtime.js");
	assert(f && "runtime.js is there");
	assert(duk_peval_string_noresult(m_interp, f->c_str()) == 0 && "runtime.js runs");

	extend_node(m_interp);
	inject_console(m_interp);
	inject_document(m_interp);
}

JSContext::~JSContext() {
	duk_destroy_heap(m_interp);
}

bool JSContext::run(std::string_view code) {
	// todo: use result, keep error around so we can report it
	// duk_pcompile_string_filename
	return duk_peval_lstring_noresult(m_interp, code.data(), code.size()) == 0;
}

bool JSContext::dispatch_event(std::string_view type, std::shared_ptr<Element> el) {
	auto h = m_node_to_handle.find(el);
	int handle = h != m_node_to_handle.end() ? h->second : -1;
	std::string event_js { std::format("new Node({}).dispatchEvent(new Event(\"{}\"))", handle, type) };
	assert(duk_peval_string(m_interp, event_js.c_str()) == 0);
	bool do_default = duk_to_boolean(m_interp, -1);
	duk_pop(m_interp);
	return !do_default;
}
