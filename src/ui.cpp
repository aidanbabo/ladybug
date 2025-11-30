#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_directory.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

#include <duktape.h>

#include <cassert>
#include <functional>
#include <iostream>
#include <numeric>
#include <sstream>

#include "layout.hpp"
#include "draw.hpp"
#include "utils.hpp"

#include "ui.hpp"

static void initialize_texture(SDL_Renderer *renderer, int width, int height, SDL_Texture *&texture, sk_sp<SkSurface> &root_surface, SkImageInfo &info) {
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, width, height);

	info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
	SkSurfaceProps surface_props;
	size_t row_bytes = width * 4;
	root_surface = SkSurfaces::Raster(info, row_bytes, &surface_props);
	assert(root_surface);
}

static FontType load_fonts(
	sk_sp<SkFontMgr> font_mgr,
	char const* normal_path,
	char const* bold_path,
	char const* italic_path,
	char const* bold_italic_path
) {
	sk_sp<SkTypeface> normal      = font_mgr->makeFromFile(normal_path);
	sk_sp<SkTypeface> bold        = font_mgr->makeFromFile(bold_path);
	sk_sp<SkTypeface> italic      = font_mgr->makeFromFile(italic_path);
	sk_sp<SkTypeface> bold_italic = font_mgr->makeFromFile(bold_italic_path);
	assert(normal);
	assert(bold);
	assert(italic);
	assert(bold_italic);

	FontType font = FontType {
		.normal = normal,
		.bold = bold,
		.italic = italic,
		.bold_italic = bold_italic,
	};
	return font;
}

static std::string url_encode_parameters(std::vector<std::pair<std::string, std::string>> params) {
	if (params.empty()) {
		return "";
	}
	std::string out{};
	for (size_t i = 0; i < params.size(); i++) {
		if (i != 0) {
			out.push_back('&');
		}
		out.append(url_encode(params[i].first));
		out.push_back('=');
		out.append(url_encode(params[i].second));
	}
	return out;
}


// todo: I don't like how partially constructed a Tab object is, there is no need for m_url to be null just for
// Cross-Site Requests
void Tab::load(HttpRequest request, bool alter_history) {
	if (request.method == HttpMethod::GET && m_url && m_url->equal_disregarding_fragment(request.url)) {
		if (request.url.fragment == "") {
			m_scroll = 0;
		} else {
			std::vector<std::shared_ptr<LayoutBase>> layouts;
			layout_tree_to_list(m_document, layouts);
			auto layout = std::find_if(layouts.begin(), layouts.end(), [&](auto lay) {
				for (auto const& n : lay->nodes()) {
					if (n->type != NodeType::Element) {
						continue;
					}
					auto e = static_cast<Element const&>(*n);
					if (auto id = e.attributes.find("id"); id != e.attributes.end()) {
						if (id->second == request.url.fragment) {
							return true;
						}
					}
				}
				return false;
			});

			if (layout == layouts.end()) {
				std::cerr << "Failed to locate fragment" << std::endl;
				return;
			} else {
				m_scroll = (*layout)->m_y;
				clamp_scroll();
			}
		}

		m_url = request.url;
	} else {
		auto response = m_browser.m_connection_manager.request(request, m_url);
		if (!response) {
			std::cerr << "Failed to load new document" << std::endl;
			return;
		}

		m_allowed_origins = std::nullopt;

		if (auto f = response->headers.find("content-security-policy"); f != response->headers.end()) {
			auto const& csp = split(f->second, " ");
			if (!csp.empty() && csp[0] == std::string_view{"default-src"}) {
				m_allowed_origins = std::unordered_set<std::string>{};
				for (size_t i = 1; i < csp.size(); i++) {
					if (auto u = URL::create(csp[i])) {
						m_allowed_origins->insert(u->origin());
					}
				}
			}
		}

		m_scroll = 0;
		m_nodes = HTMLParser(response->body).parse();
		assert(m_nodes->type == NodeType::Element);
		m_rules = m_default_style_sheet;
		std::vector<std::shared_ptr<Node>> nodes;
		html_tree_to_list(m_nodes, nodes);
		// todo: Find a way to reuse the style and script fetching/executing code.
		// True means we must fetch and false means inline.
		std::vector<std::pair<std::string, bool>> styles;
		for (auto const& node : nodes) {
			if (node->type != NodeType::Element) {
				continue;
			}
			auto element = static_cast<Element const&>(*node);
			if (element.tag == "style" && !element.children.empty()) {
				Node const& child = *element.children[0];
				if (child.type == NodeType::Text) {
					auto text = static_cast<Text const&>(child);
					styles.push_back(std::make_pair(text.text, false));
				}
			} else {
				if (element.tag != "link") {
					continue;
				}
				auto rel = element.attributes.find("rel");
				if (rel == element.attributes.end() || rel->second != "stylesheet") {
					continue;
				}
				if (!element.attributes.contains("href")) {
					continue;
				}
				styles.push_back(std::make_pair(element.attributes["href"], true));
			}
		}

		for (auto const& [access, must_fetch] : styles) {
			std::string body;
			if (must_fetch) {
				auto style_url = request.url.resolve(access);
				if (!style_url) {
					std::cerr << "Skipping malformed url: '" << access << "'" << std::endl;
					continue;
				}
				if (!allowed_request(*style_url)) {
					std::cerr << "Blocked stylesheet at: '" << access << "' due to CSP" << std::endl;
					continue;
				}
				auto maybe_response = m_browser.m_connection_manager.request(*style_url, request.url);
				if (!maybe_response) {
					std::cerr << "Error fetching stylesheet at: '" << access << "'" << std::endl;
					continue;
				}
				body = maybe_response->body;
			} else {
				body = access;
			}
			auto sh = CSSParser(body).parse();
			if (!sh) {
				if (must_fetch) {
					std::cerr << "Improper stylesheet at: '" << access << "'" << std::endl;
				} else {
					std::cerr << "Improper inline stylesheet" << std::endl;
				}
				continue;
			}
			m_rules.rules.insert(m_rules.rules.end(), sh->rules.begin(), sh->rules.end());
		}

		// stable sort so file order breaks the rule (first file first!)
		std::ranges::stable_sort(m_rules.rules, {}, [](auto p) {
			return p.first->priority;
		});

		std::vector<std::pair<std::string, bool>> scripts;
		for (auto const& node : nodes) {
			if (node->type != NodeType::Element) {
				continue;
			}

			auto element = static_cast<Element const&>(*node);
			if (element.tag == "script" && !element.children.empty()) {
				Node const& child = *element.children[0];
				if (child.type == NodeType::Text) {
					auto text = static_cast<Text const&>(child);
					scripts.push_back(std::make_pair(text.text, false));
				}
			} else {
				if (element.tag != "script") {
					continue;
				}
				if (auto f = element.attributes.find("src"); f != element.attributes.end()) {
					scripts.push_back(std::make_pair(f->second, true));
				}
			}
		}

		// URL must be initialized before javascript runs.
		m_url = request.url;

		if (m_js.has_value()) {
			m_js.reset();
		}
		m_js.emplace(*this);
		for (auto const& [access, must_fetch] : scripts) {
			if (must_fetch) {
				auto script_url = request.url.resolve(access);
				if (!script_url) {
					std::cerr << "Skipping malformed url: '" << access << "'" << std::endl;
					continue;
				}
				if (!allowed_request(*script_url)) {
					std::cerr << "Blocked script at: '" << access << "' due to CSP" << std::endl;
					continue;
				}
				auto code = m_browser.m_connection_manager.request(*script_url, request.url);
				if (!code) {
					std::cerr << "Error fetching script at: '" << access << "'" << std::endl;
					continue;
				}
				if (!m_js->run(code->body)) {
					std::cerr << "Script " << *script_url << " crashed" << std::endl;;
				}
			} else {
				if (!m_js->run(access)) {
					std::cerr << "Inline script crashed" << std::endl;;
				}
			}

		}

		render();
	}

	if (alter_history) {
		m_history.erase(m_history.begin() + m_history_index + 1, m_history.end());
		m_history.push_back(request);
		m_history_index++;
	}
}

std::optional<HttpRequest> Tab::submit_form(std::shared_ptr<Element> node) {
	bool do_default = true;
	auto bubble = node;
	while (bubble) {
		auto res = m_js->dispatch_event("submit", node);
		if (!res.do_default) do_default = false;
		if (!res.propagate) break;
		bubble = bubble->parent.lock();
	}

	if (!do_default) {
		return std::nullopt;
	}

	auto url = m_url->resolve(node->attributes["action"]);
	if (!url) {
		return std::nullopt;
	}

	std::vector<std::shared_ptr<Node>> inputs;
	html_tree_to_list(node, inputs);
	inputs.erase(std::remove_if(inputs.begin(), inputs.end(), [](auto n){
		if (n->type != NodeType::Element) {
			return true;
		}
		auto el = std::static_pointer_cast<Element>(n);
		return el->tag != "input" || !el->attributes.contains("name");
	}), inputs.end());

	std::vector<std::pair<std::string, std::string>> params;
	for (auto const& node : inputs) {
		auto input = std::static_pointer_cast<Element>(node);
		std::string name { input->attributes["name"] };
		std::string value;
		auto v = input->attributes.find("value");
		if (v != input->attributes.end()) {
			value = v->second;
		}
		params.push_back({std::move(name), std::move(value)});
	}

	HttpMethod method;
	auto method_iter = node->attributes.find("method");
	if (method_iter == node->attributes.end()) {
		method = HttpMethod::GET;
	} else {
		std::string m = method_iter->second;
		std::transform(m.begin(), m.end(), m.begin(), ::tolower);
		if (m == "get") {
			method = HttpMethod::GET;
		} else if (m == "post") {
			method = HttpMethod::POST;
		} else {
			std::cerr << "unknown form method: " << m << std::endl;
			method = HttpMethod::GET;
		}
	}
	std::optional<std::string> payload = std::nullopt;
	if (method == HttpMethod::POST) {
		payload = url_encode_parameters(params);
	} else if (method == HttpMethod::GET) {
		url->path += "?" + url_encode_parameters(params);
	}
	return HttpRequest(*url, method, url_encode_parameters(params));
}

void Tab::render() {
	m_nodes->style(m_rules);
	//print_node(*m_nodes);
	m_document = std::make_shared<DocumentLayout>(m_nodes, m_width);
	m_document->layout(m_browser.m_font_cache);
	//m_document->print_layout();
	m_display_list.clear();
	paint_tree(*m_document, m_display_list);
}
	
void Tab::draw(SkCanvas *canvas, float offset) {
	// content
	for (auto command : m_display_list) {
		if (command->rect.fTop > m_scroll + m_height) continue;
		if (command->rect.fBottom < m_scroll) continue;
		command->execute(m_scroll - offset, *canvas);
	}

	// scrollbar
	if (m_height < m_document->m_height) {
		float scrollbar_ratio = (float) m_height / (float) m_document->m_height;
		float scrollbar_size = scrollbar_ratio * (float) m_height;
		float scrollbar_start = scrollbar_ratio * (float) m_scroll;
		SkPaint paint;
		paint.setColor(SK_ColorBLUE);
		canvas->drawRect(SkRect::MakeLTRB(m_width - HSTEP, scrollbar_start + offset, m_width, scrollbar_start + scrollbar_size + offset), paint);
	}

}

std::optional<std::string> Tab::title() {
	std::vector<std::shared_ptr<Node>> nodes;
	html_tree_to_list(m_nodes, nodes);
	auto el = std::find_if(nodes.begin(), nodes.end(), [](auto n) {
		if (n->type != NodeType::Element) {
			return false;
		}
		auto e = static_cast<Element const&>(*n);
		return e.tag == "title";
	});
	if (el == nodes.end()) {
		return std::nullopt;
	} else {
		auto children = (*el)->children;
		if (children.size() != 1 || children[0]->type != NodeType::Text) {
			return std::nullopt;
		}
		return static_cast<Text const&>(*children[0]).text;
	}
}

URL const& Tab::url() const {
	return *m_url;
}

bool Tab::allowed_request(URL const& url) const {
	return m_allowed_origins == std::nullopt || m_allowed_origins->find(url.origin()) != m_allowed_origins->end();
}

void Tab::blur() {
	if (m_focus) {
		m_focus->is_focused = false;
		m_focus = nullptr;
	}
}

bool Tab::can_go_back() const {
	return m_history_index > 0;
}

HistoryNavigationAttempt Tab::go_back(bool force) {
	if (can_go_back()) {
		HttpRequest const& request = m_history[m_history_index - 1];
		if (request.method == HttpMethod::GET || force) {
			m_history_index--;
			load(request, false);
			return HistoryNavigationAttempt::Navigated;
		} else {
			return HistoryNavigationAttempt::NeedsConfirmation;
		}
	}
	return HistoryNavigationAttempt::NoNavigation;
}

bool Tab::can_go_forward() const {
	return m_history_index < m_history.size() - 1;
}

HistoryNavigationAttempt Tab::go_forward(bool force) {
	if (can_go_forward()) {
		HttpRequest const& request = m_history[m_history_index + 1];
		if (request.method == HttpMethod::GET || force) {
			m_history_index++;
			load(request, false);
			return HistoryNavigationAttempt::Navigated;
		} else {
			return HistoryNavigationAttempt::NeedsConfirmation;
		}
	}
	return HistoryNavigationAttempt::NoNavigation;
}

void Tab::clamp_scroll() {
	if (m_scroll < 0) {
		m_scroll = 0;
	}

	int max_scroll = m_document->m_height + 2 * VSTEP - m_height;
	if (max_scroll < 0) {
		m_scroll = 0;
	} else if (m_scroll > max_scroll) {
		m_scroll = max_scroll;
	}
}

void Tab::scroll_up() {
	m_scroll -= SCROLL_STEP;
	clamp_scroll();
}

void Tab::scroll_down() {
	m_scroll += SCROLL_STEP;
	clamp_scroll();
}

[[nodiscard]]
std::optional<HttpRequest> Tab::keypress(SDL_KeyboardEvent event) {
	if (m_focus != nullptr) {
		bool do_default = true;
		auto n = m_focus;
		while (n) {
			auto res = m_js->dispatch_event("keydown", m_focus);
			if (!res.do_default) do_default = false;
			if (res.propagate) break;
			n = n->parent.lock();
		}
		if (do_default) {
			if (event.key >= 0x20 && event.key < 0x7f) {
				char c = SDL_GetKeyFromScancode(event.scancode, event.mod, false);
				m_focus->attributes["value"].push_back(c);
				render();
			} else if (event.key == SDLK_RETURN) {
				auto el = m_focus;
				while (el != nullptr) {
					if (el->tag == "form" && el->attributes.contains("action")) {
						return submit_form(el);
					}
					auto parent = el->parent.lock();
					assert(parent->type == NodeType::Element);
					el = std::static_pointer_cast<Element>(parent);
				}
			}
		}
	}
	return std::nullopt;
}

[[nodiscard]]
std::optional<HttpRequest> Tab::click(float x, float y) {
	if (m_focus) {
		m_focus->is_focused = false;
		m_focus = nullptr;
	}
	y += m_scroll;
	std::vector<std::shared_ptr<LayoutBase>> objs;
	layout_tree_to_list(m_document, objs);
	objs.erase(std::remove_if(objs.begin(), objs.end(), [x, y](std::shared_ptr<LayoutBase> n) -> bool {
		bool b =  n->m_x > x || x >= n->m_x + n->m_width || n->m_y > y || y >= n->m_y + n->m_height;
		return b;
	}), objs.end());
	if (objs.empty()) {
		return std::nullopt;
	}
	// Ignore block nodes. We don't really click on them meaninfully, so we don't have to accound for multiple nodes per layout.
	auto elt_node = objs.back()->nodes()[0];
	auto elt = [&]() -> std::shared_ptr<Element> {
		if (elt_node->type == NodeType::Element) {
			return std::static_pointer_cast<Element>(elt_node);
		} else {
			return elt_node->parent.lock();
		}
	}();
	if (elt) {
		while (elt != nullptr) {
			auto el = std::static_pointer_cast<Element>(elt);

			auto res = m_js->dispatch_event("click", el);
			if (res.do_default) {
				if (el->tag == "a") {
					if (auto href = el->attributes.find("href"); href != el->attributes.end()) {
						if (auto url = m_url->resolve(href->second)) {
							return *url;
						}
					}
				} else if (el->tag == "input") {
					el->attributes["value"] = "";
					m_focus = el;
					el->is_focused = true;
				} else if (el->tag == "button") {
					while (el != nullptr) {
						if (el->tag == "form" && el->attributes.contains("action")) {
							return submit_form(el);
						}
						auto parent = el->parent.lock();
						assert(parent->type == NodeType::Element);
						el = std::static_pointer_cast<Element>(parent);
					}
				}
			}

			if (!res.propagate) {
				break;
			}
			elt = elt->parent.lock();
		}
		render();
	}
	return std::nullopt;
}

void Tab::resize(int new_width, int new_height) {
	m_width = new_width;
	m_height = new_height;
	m_document = std::make_shared<DocumentLayout>(m_nodes, m_width);
	m_document->layout(m_browser.m_font_cache);
	m_display_list.clear();
	paint_tree(*m_document, m_display_list);
	clamp_scroll();

}

Tab::Tab(int width, int height, Browser& browser)
	: m_width(width)
	, m_height(height)
	, m_default_style_sheet()
	, m_document()
	, m_display_list()
	, m_browser(browser)
{
	StyleSheet default_style_sheet{};
	if (auto css_string = read_entire_file_to_string("runtime_support/browser.css")) {
		std::optional<StyleSheet> default_css { CSSParser(*css_string).parse() };
		assert(default_css.has_value() && "invalid browser.css");
		m_default_style_sheet = *default_css;
	}
}

// todo: handle resize (currently nothing reasonable comes to mind)
Chrome::Chrome(FontCache& font_cache, int width) {
	m_width = width;
	m_font = font_cache.get_font("times", 20, false, false);
	SkFontMetrics m;
	m_font->getMetrics(&m);
	m_font_height = m.fDescent - m.fAscent;
	m_padding = 5;
	m_tabbar_top = 0;
	m_tabbar_bottom = m_font_height + 2 * m_padding;
	m_plus_width = m_font->measureText("+", 1, SkTextEncoding::kUTF8) + 2 * m_padding;
	m_newtab_rect = SkRect::MakeLTRB(m_padding, m_padding, m_padding + m_plus_width, m_padding + m_font_height);
	m_urlbar_top = m_tabbar_bottom;
	m_urlbar_bottom = m_urlbar_top + m_font_height + 2 * m_padding;
	m_bottom = m_urlbar_bottom;

	m_back_width = m_font->measureText("<", 1, SkTextEncoding::kUTF8) + 2 * m_padding;
	m_back_rect = SkRect::MakeLTRB(m_padding, m_urlbar_top + m_padding, m_padding + m_back_width, m_urlbar_bottom - m_padding);
	m_forward_width = m_font->measureText(">", 1, SkTextEncoding::kUTF8) + 2 * m_padding;
	m_forward_rect = SkRect::MakeLTRB(m_back_rect.fRight + m_padding, m_urlbar_top + m_padding, m_back_rect.fRight + m_padding + m_forward_width, m_urlbar_bottom - m_padding);
	m_address_rect = SkRect::MakeLTRB(m_forward_rect.fRight + m_padding, m_urlbar_top + m_padding, m_width - m_padding, m_urlbar_bottom - m_padding);
}

SkRect Chrome::tab_rect(int index) {
	auto tab_start = m_newtab_rect.fRight + m_padding;
	auto tab_width = m_font->measureText("Tab X", 5, SkTextEncoding::kUTF8) + 2 * m_padding;
	return SkRect::MakeLTRB(
		tab_start + tab_width * index, 
		m_tabbar_top,
		tab_start + tab_width * (index + 1), 
		m_tabbar_bottom);
}

void Chrome::keypress(char c) {
	if (m_focus == Focus::AddressBar) {
		m_address_bar.push_back(c);
	}
}

void Chrome::backspace() {
	if (m_focus == Focus::AddressBar && !m_address_bar.empty()) {
		m_address_bar.pop_back();
	}
}

void Chrome::blur() {
	m_focus = Focus::Nothing;
}

void Chrome::enter(Browser& browser) {
	if (m_focus == Focus::AddressBar) {
		if (auto url = URL::create(m_address_bar)) {
			browser.m_tabs[browser.m_active_tab]->load(*url, true);
			m_focus = Focus::Nothing;
			browser.set_window_title();
		}
	}
}
	
void Chrome::click(Browser& browser, float x, float y) {
	// todo: reset this for clicks outside the chrome
	m_focus = Focus::Nothing;
	if (m_newtab_rect.contains(x, y)) {
		browser.new_tab(URL::create("https://browser.engineering").value_or(URL::ABOUT_BLANK));
	} else if (m_back_rect.contains(x, y)) {
		auto navigated = browser.m_tabs[browser.m_active_tab]->go_back();
		switch (navigated) {
                case HistoryNavigationAttempt::NoNavigation:
			break;
                case HistoryNavigationAttempt::NeedsConfirmation:
			browser.navigation_confirmation_popup(true);
			break;
                case HistoryNavigationAttempt::Navigated:
			browser.set_window_title();
			break;
                }
	} else if (m_forward_rect.contains(x, y)) {
		auto navigated = browser.m_tabs[browser.m_active_tab]->go_forward();
		switch (navigated) {
                case HistoryNavigationAttempt::NoNavigation:
			break;
                case HistoryNavigationAttempt::NeedsConfirmation:
			browser.navigation_confirmation_popup(false);
			break;
                case HistoryNavigationAttempt::Navigated:
			browser.set_window_title();
			break;
                }
	} else if (m_address_rect.contains(x, y)) {
		m_focus = Focus::AddressBar;
		m_address_bar = std::string{};
	} else {
		for (size_t i = 0; i < browser.m_tabs.size(); i++) {
			if (tab_rect(i).contains(x, y)) {
				browser.set_active_tab(i);
				break;
			}
		}
	}
}

void Chrome::paint(Browser const& browser, std::vector<std::shared_ptr<DrawCommand>>& commands) {
	commands.push_back(DrawRect::create(SkRect::MakeLTRB(0, 0, browser.m_width, m_bottom), SK_ColorWHITE));
	
	commands.push_back(DrawOutline::create(m_newtab_rect, SK_ColorBLACK, 1));
	commands.push_back(DrawText::create(m_newtab_rect.fLeft + m_padding, m_newtab_rect.fTop, m_plus_width, "+", m_font, SK_ColorBLACK));

	for (size_t i = 0; i < browser.m_tabs.size(); i++) {
		auto bounds = tab_rect(i);
		commands.push_back(DrawLine::create(bounds.fLeft, 0, bounds.fLeft, bounds.fBottom, SK_ColorBLACK, 1));
		commands.push_back(DrawLine::create(bounds.fRight, 0, bounds.fRight, bounds.fBottom, SK_ColorBLACK, 1));
		std::string text = "Tab " + std::to_string(i);
		commands.push_back(DrawText::create(bounds.fLeft + m_padding, bounds.fTop + m_padding, text, m_font, SK_ColorBLACK));

		if (i == browser.m_active_tab) {
			commands.push_back(DrawLine::create(0, bounds.fBottom, bounds.fLeft, bounds.fBottom, SK_ColorBLACK, 1));
			commands.push_back(DrawLine::create(bounds.fRight, bounds.fBottom, browser.m_width, bounds.fBottom, SK_ColorBLACK, 1));
		}
	}

	SkColor backward_color = browser.m_tabs[browser.m_active_tab]->can_go_back() ? SK_ColorBLACK : SK_ColorLTGRAY;
	commands.push_back(DrawOutline::create(m_back_rect, backward_color, 1));
	commands.push_back(DrawText::create(m_back_rect.fLeft + m_padding, m_back_rect.fTop, m_back_width, "<", m_font, backward_color));

	SkColor forward_color = browser.m_tabs[browser.m_active_tab]->can_go_forward() ? SK_ColorBLACK : SK_ColorLTGRAY;
	commands.push_back(DrawOutline::create(m_forward_rect, forward_color, 1));
	commands.push_back(DrawText::create(m_forward_rect.fLeft + m_padding, m_forward_rect.fTop, m_forward_width, ">", m_font, forward_color));

	commands.push_back(DrawOutline::create(m_address_rect, SK_ColorBLACK, 1));

	if (m_focus == Focus::AddressBar) {
		float address_bar_size = m_font->measureText(m_address_bar.data(), m_address_bar.size(), SkTextEncoding::kUTF8);
		commands.push_back(DrawText::create(m_address_rect.fLeft + m_padding, m_address_rect.fTop, address_bar_size, m_address_bar, m_font, SK_ColorBLACK));
		commands.push_back(DrawLine::create(
			m_address_rect.fLeft + m_padding + address_bar_size,
			m_address_rect.fTop,
			m_address_rect.fLeft + m_padding + address_bar_size,
			m_address_rect.fBottom,
			SK_ColorBLACK, 1));
	} else {
		URL url = browser.m_tabs[browser.m_active_tab]->url();
		std::ostringstream str;
		str << url;
		commands.push_back(DrawText::create(m_address_rect.fLeft + m_padding, m_address_rect.fTop, str.str(), m_font, SK_ColorBLACK));
	}
}

PopUp::PopUp(FontCache& font_cache, int width, std::vector<std::string> prompts, std::vector<std::string> options, std::vector<std::function<void(Browser &)>> actions) {
	assert(options.size() == actions.size());
	m_font = font_cache.get_font("times", 16, false, false);
	SkFontMetrics m;
	m_font->getMetrics(&m);
	m_font_height = m.fDescent - m.fAscent;
	m_prompts = prompts;
	m_options = options;
	m_padding = 5;
	m_option_padding = 20;
	m_actions = actions;

	std::vector<float> options_widths;
	auto text_measure = [&](auto t) { return m_font->measureText(t.data(), t.size(), SkTextEncoding::kUTF8); };
	std::transform(m_prompts.begin(), m_prompts.end(), std::back_inserter(m_prompts_widths), text_measure);
	std::transform(m_options.begin(), m_options.end(), std::back_inserter(options_widths), text_measure);
	m_width = std::ranges::max(m_prompts_widths);
	float options_width = std::reduce(options_widths.begin(), options_widths.end(), 0.0, [](float a, float f) { return a + f; });
	m_width = std::max(m_width, options_width + (m_options.size() + 1) * m_option_padding);
	m_width += 2 * m_padding;

	m_top = 0;
	m_left = (width - m_width) / 2.0;
	float options_top = (m_prompts.size() + 1) * (m_font_height + m.fLeading) + m_padding;
	m_height = options_top + m_font_height + m.fLeading + m_padding;

	float options_cursor = m_left;
	for (size_t i = 0; i < m_options.size(); i++) {
		options_cursor += m_option_padding;
		m_options_rects.push_back(SkRect::MakeLTRB(options_cursor, options_top, options_cursor + options_widths[i], options_top + m_font_height));
		options_cursor += options_widths[i];
	}

	m_rect = SkRect::MakeLTRB(m_left, m_top, m_left + m_width, m_top + m_height);
}


void PopUp::click(Browser& browser, float x, float y) {
	for (size_t i = 0; i < m_options_rects.size(); i++) {
		if (m_options_rects[i].contains(x, y)) {
			(m_actions[i])(browser);
		}
	}
}

void PopUp::paint(std::vector<std::shared_ptr<DrawCommand>>& commands) {
	commands.push_back(DrawRect::create(m_rect, SK_ColorWHITE));
	commands.push_back(DrawOutline::create(m_rect, SK_ColorBLACK, 1));

	SkFontMetrics m;
	m_font->getMetrics(&m);
	for (size_t i = 0; i < m_prompts.size(); i++) {
		commands.push_back(DrawText::create(m_left + m_padding, m_top + m_padding + (i * (m_font_height + m.fLeading)), m_prompts_widths[i], m_prompts[i], m_font, SK_ColorBLACK));
	}

	for (size_t i = 0; i < m_options.size(); i++) {
		commands.push_back(DrawOutline::create(m_options_rects[i], SK_ColorBLACK, 1));
		commands.push_back(DrawText::create(m_options_rects[i], m_options[i], m_font, SK_ColorBLACK));
	}
}

std::optional<std::unique_ptr<Browser>> Browser::create() {
	int width = INITIAL_WIDTH;
	int height = INITIAL_HEIGHT;
	// todo: change to OpenGL
	SDL_Window *window = SDL_CreateWindow("Ladybug", width, height, SDL_WINDOW_RESIZABLE);

	if (window == nullptr) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create window: %s\n", SDL_GetError());
		return std::nullopt;
	}

	SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);

	SDL_Texture *texture;
	SkImageInfo info;
	sk_sp<SkSurface> root_surface;
	initialize_texture(renderer, width, height, texture, root_surface, info);

	sk_sp<SkFontMgr> font_mgr = SkFontMgr_New_Custom_Directory("./fonts");
	assert(font_mgr);

	FontType times_new_roman = load_fonts(
		font_mgr,
		"./fonts/Times New Roman.ttf",
		"./fonts/Times New Roman Bold.ttf",
		"./fonts/Times New Roman Italic.ttf",
		"./fonts/Times New Roman Bold Italic.ttf"
	);

	FontType courier_new = load_fonts(
		font_mgr,
		"./fonts/cour.ttf",
		"./fonts/courbd.ttf",
		"./fonts/couri.ttf",
		"./fonts/courbi.ttf"
	);

	FontCache font_cache;
	font_cache.add_type("times", times_new_roman);
	font_cache.add_type("courier new", courier_new);

	return std::make_unique<Browser>(width, height, window, renderer, texture, root_surface, info, font_mgr, font_cache);
}

void Browser::new_tab(HttpRequest request) {
	auto new_tab = std::make_unique<Tab>(m_width, m_height - m_chrome.m_bottom, *this);
	new_tab->load(request, true);
	m_tabs.push_back(std::move(new_tab));
	set_active_tab(m_tabs.size() - 1);
	draw();
}

void Browser::set_active_tab(size_t index) {
	m_active_tab = index;
	set_window_title();
}

void Browser::set_window_title() {
	if (auto title = m_tabs[m_active_tab]->title()) {
		SDL_SetWindowTitle(m_window, title->data());
	} else {
		SDL_SetWindowTitle(m_window, "Ladybug");
	}
}

void Browser::draw() {
	auto canvas = m_root_surface->getCanvas();
	canvas->clear(SK_ColorWHITE);

	m_tabs[m_active_tab]->draw(canvas, m_chrome.m_bottom);
	
	std::vector<std::shared_ptr<DrawCommand>> chrome_draws;
	m_chrome.paint(*this, chrome_draws);
	for (auto const& cmd : chrome_draws) {
		cmd->execute(0, *canvas);
	}

	if (m_popup) {
		std::vector<std::shared_ptr<DrawCommand>> popup_draws;
		m_popup->paint(popup_draws);
		for (auto const& cmd : popup_draws) {
			cmd->execute(0, *canvas);
		}
	}

	sk_sp<SkImage> image = m_root_surface->makeImageSnapshot();

	size_t row_bytes = m_width * 4;

	std::vector<uint8_t> pixels(m_height * m_width * 4);
	if (!image->readPixels(m_surface_info, pixels.data(), row_bytes, 0, 0)) {
		assert(false);
	}

	SDL_UpdateTexture(m_texture, nullptr, pixels.data(), row_bytes);

	SDL_RenderClear(m_renderer);
	SDL_RenderTexture(m_renderer, m_texture, nullptr, nullptr);
	SDL_RenderPresent(m_renderer);
}

void Browser::resize(int new_width, int new_height) {
	m_width = new_width;
	m_height = new_height;
	SDL_DestroyTexture(m_texture);

	initialize_texture(m_renderer, m_width, m_height, m_texture, m_root_surface, m_surface_info);

	for (auto& tab : m_tabs) {
		tab->resize(new_width, new_height);
	}
	draw();
}

void Browser::scroll_up() {
	m_tabs[m_active_tab]->scroll_up();
	draw();
}

void Browser::scroll_down() {
	m_tabs[m_active_tab]->scroll_down();
	draw();
}

void Browser::handle_click(ClickType type, float x, float y) {
	if (m_focus == Focus::Alert) {
		m_popup->click(*this, x, y);
	} else if (y < m_chrome.m_bottom) {
		m_focus = Focus::Chrome;
		m_tabs[m_active_tab]->blur();
		m_tabs[m_active_tab]->render();
		if (type == ClickType::Left) {
			m_chrome.click(*this, x, y);
		}
	} else {
		m_focus = Focus::Content;
		m_chrome.blur();
		if (type != ClickType::Right) {
			float tab_y = y - m_chrome.m_bottom;
			auto request = m_tabs[m_active_tab]->click(x, tab_y);
			if (request) {
				if (type == ClickType::Left) {
					m_tabs[m_active_tab]->load(*request, true);
				} else {
					new_tab(*request);
				}
				set_window_title();
			}
		}
	}
	draw();
}

void Browser::handle_key(SDL_KeyboardEvent event) {
	if (event.type == SDL_EVENT_KEY_UP) {
		return;
	}

	if (event.key == SDLK_DOWN) {
		scroll_down();
	} else if (event.key == SDLK_UP) {
		scroll_up();
	} else if (m_focus == Focus::Nothing) {
		// nothing
	} else if (m_focus == Focus::Alert) {
		// todo: add keyboard input here?
	} else if (m_focus == Focus::Chrome) {
		if (event.key == SDLK_RETURN) {
			m_chrome.enter(*this);
		} else if (event.key == SDLK_BACKSPACE) {
			m_chrome.backspace();
		} else if (event.key >= 0x20 && event.key < 0x7f) {
			char c = SDL_GetKeyFromScancode(event.scancode, event.mod, false);
			m_chrome.keypress(c);
		}
	} else if (m_focus == Focus::Content) {
		auto navigation = m_tabs[m_active_tab]->keypress(event);
		if (navigation) {
			m_tabs[m_active_tab]->load(*navigation, true);
		}
	} else {
		assert(false && "unreachable");
	}
	draw();
}

void Browser::navigation_confirmation_popup(bool going_back) {
	m_focus = Focus::Alert;

	auto close = [](Browser& b){
		b.m_focus = Focus::Nothing;
		b.m_popup = std::nullopt;
	};

	std::vector<std::string> prompts{}, options{"Cancel"};
	std::vector<std::function<void(Browser&)>> actions {close};
	if (going_back) {
		prompts.push_back("Going back will send data again.");
		prompts.push_back("Are you sure you want to go back?");
		options.push_back("Go back");
		actions.push_back([close](Browser& b){
			close(b);
			b.m_tabs[b.m_active_tab]->go_back(true);
		});
	} else {
		prompts.push_back("Going forward will send data again.");
		prompts.push_back("Are you sure you want to go forward?");
		options.push_back("Go forward");
		actions.push_back([close](Browser& b){
			close(b);
			b.m_tabs[b.m_active_tab]->go_forward(true);
		});
	}

	m_popup = PopUp(m_font_cache, m_width, prompts, options, actions);
}

void Browser::destroy() {
	SDL_DestroyTexture(m_texture);
	SDL_DestroyRenderer(m_renderer);
	SDL_DestroyWindow(m_window);
}

Browser::Browser(
	int width,
	int height,
	SDL_Window *window,
	SDL_Renderer *renderer,
	SDL_Texture *texture,
	sk_sp<SkSurface> root_surface,
	SkImageInfo surface_info,
	sk_sp<SkFontMgr> font_mgr,
	FontCache font_cache
)
	: m_width(width)
	, m_height(height)
	, m_window(window)
	, m_renderer(renderer)
	, m_texture(texture)
	, m_root_surface(root_surface)
	, m_surface_info(surface_info)
	, m_font_mgr(font_mgr)
	, m_font_cache(font_cache)
	, m_chrome(font_cache, width)
{}

Browser::~Browser() = default;
