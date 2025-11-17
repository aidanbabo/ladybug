// todo: Exercise 2-5: Emoji -> done with text stuff for now
// todo: Exercise 3-4: Small caps -> done with text stuff for now
// todo: Exercise 3-5: Preformatted text -> done with text stuff for now
// todo: Exercise 4-5: Syntax highlighting -> assuming CSS will make the implementation better/not interested
// todo: Exercise 4-6: Mis-nested formatting tags -> assuming we get to it a little better later
// todo: Exercise 5-4: Table of contents -> seems like not a real thing? maybe a real use case will appear later on?
// todo: Exercise 5-6: Run-ins -> Browser support is already poor and the feature is too niche
// todo: Exercise 6-9: !important -> Not interested
// todo: Exercise 6-10: :has selectors -> Also not interested AND it would be slow AND maybe I could find a fast impl but i'm NOT interested
// todo: Exercise 7-6: Search -> I don't want to add more URL parsing unless I must, because it's too close to a refactor
// todo: Exercise 7-7: Visited links -> Not intersted in doing it in C++ or CSS
// todo: Exercise 7-8: Bookmarks -> I don't want to design a web page in Skia or HTML with hooks
// todo: Exercise 7-9: Cursor -> Cumbersome. Maybe I'll want it later
// todo: Exercise 7-10: Mulitple Windows -> This seems a litle cool, but I'm not interested enough in dealing with SDL3 more
// todo: Exercise 7-11: Clicks via the display list -> I am too worried about the display list changes coming up to do this
// Many exercises are being skipped because it makes the refactors in later chapters much harder. Chapter 7 was a pain to get done.

// todo: reimplement <sup> (and then maybe <sub>?) now that we are using stylesheets instead of code
//
// todo: erase_if replaces the erase-remove idiom c++20

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// Skia
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_directory.h"

#include <iostream>

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

int const INITIAL_WIDTH  = 800;
int const INITIAL_HEIGHT = 600;
int const SCROLL_STEP = 100;

#include "draw.hpp"
#include "layout.hpp"
#include "network.hpp"
#include "utils.hpp"
#include "parsers.hpp"

void initialize_texture(SDL_Renderer *renderer, int width, int height, SDL_Texture *&texture, sk_sp<SkSurface> &root_surface, SkImageInfo &info) {
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, width, height);

	info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
	SkSurfaceProps surface_props;
	size_t row_bytes = width * 4;
	root_surface = SkSurfaces::Raster(info, row_bytes, &surface_props);
	assert(root_surface);
}

// todo: make these functions generic or make a bad boy iterator
void html_tree_to_list(std::shared_ptr<Node> node, std::vector<std::shared_ptr<Node>>& list) {
	list.push_back(node);
	for (auto const& c : node->children) {
		html_tree_to_list(c, list);
	}
}

void layout_tree_to_list(std::shared_ptr<LayoutBase> node, std::vector<std::shared_ptr<LayoutBase>>& list) {
	list.push_back(node);
	for (auto const& c : node->m_children) {
		layout_tree_to_list(c, list);
	}
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

class Tab {
	int m_width;
	int m_height;
	StyleSheet m_default_style_sheet;
	std::shared_ptr<Node> m_nodes{};
	std::shared_ptr<DocumentLayout> m_document{};
	std::vector<std::shared_ptr<DrawCommand>> m_display_list{};
	std::vector<URL> m_history{};
	size_t m_history_index = -1;
	ConnectionManager m_connection_manager;
	int m_scroll = 0;
	URL m_url = URL::ABOUT_BLANK;

public:
	void load(URL url, FontCache& font_cache, bool alter_history) {
		if (m_url.equal_disregarding_fragment(url)) {
			if (url.fragment == "") {
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
							if (id->second == url.fragment) {
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
		} else {
			std::optional<std::string> body = m_connection_manager.request(url);
			if (!body) {
				std::cerr << "Failed to load new document" << std::endl;
				return;
			}

			m_scroll = 0;
			m_nodes = HTMLParser(*body).parse();
			assert(m_nodes->type == NodeType::Element);
			StyleSheet rules = m_default_style_sheet;
			std::vector<std::shared_ptr<Node>> nodes;
			html_tree_to_list(m_nodes, nodes);
			// true means we must fetch, false means inline
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
				auto body = [&]() -> std::optional<std::string> {
					if (must_fetch) {
						auto style_url = url.resolve(access);
						if (!style_url) {
							std::cerr << "Skipping malformed url: '" << access << "'" << std::endl;
							return std::nullopt;
						}

						auto body = m_connection_manager.request(*style_url);
						return body;
					} else {
						return access;
					}
				}();
				if (!body) {
					continue;
				}
				auto sh = CSSParser(*body).parse();
				if (!sh) {
					if (must_fetch) {
						std::cerr << "Improper stylesheet at: '" << access << "'" << std::endl;
					} else {
						std::cerr << "Improper inline stylesheet" << std::endl;
					}
					continue;
				}
				rules.rules.insert(rules.rules.end(), sh->rules.begin(), sh->rules.end());
			}

			// stable sort so file order breaks the rule (first file first!)
			std::ranges::stable_sort(rules.rules, {}, [](auto p) {
				return p.first->priority;
			});
			m_nodes->style(rules);
			//print_node(*m_nodes);
			m_document = std::make_shared<DocumentLayout>(m_nodes, m_width);
			m_document->layout(font_cache);
			//m_document->print_layout();
			m_display_list.clear();
			paint_tree(*m_document, m_display_list);
		}

		m_url = url;

		if (alter_history) {
			m_history.erase(m_history.begin() + m_history_index + 1, m_history.end());
			m_history.push_back(url);
			m_history_index++;
		}
	}
	
	void draw(SkCanvas *canvas, float offset) {
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

	URL const& url() const {
		return m_url;
	}

	bool can_go_back() const {
		return m_history_index > 0;
	}

	bool go_back(FontCache& font_cache) {
		if (can_go_back()) {
			m_history_index--;
			load(m_history[m_history_index], font_cache, false);
			return true;
		}
		return false;
	}

	bool can_go_forward() const {
		return m_history_index < m_history.size() - 1;
	}

	bool go_forward(FontCache& font_cache) {
		if (can_go_forward()) {
			m_history_index++;
			load(m_history[m_history_index], font_cache, false);
			return true;
		}
		return false;
	}

	void clamp_scroll() {
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

	void scroll_up() {
		m_scroll -= SCROLL_STEP;
		clamp_scroll();
	}

	void scroll_down() {
		m_scroll += SCROLL_STEP;
		clamp_scroll();
	}

	std::optional<std::string> title() {
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

	// If there is navigation involved, a value is returned for the browser to navigate to.
	std::optional<URL> click(float x, float y) {
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
		// todo: this is a hack so we can only click on text elements AND so that we
		// don't have to make a virtual function to get the current node from an element
		// since BlockLayouts can have multiple nodes.
		if (auto text = dynamic_cast<TextLayout *>(&*objs.back())) {
			Node const *elt = &*text->m_node;
			while (elt != nullptr) {
				if (elt->type == NodeType::Element) {
					auto el = static_cast<Element const*>(elt);
					if (el->tag == "a") {
						if (auto href = el->attributes.find("href"); href != el->attributes.end()) {
							if (auto url = m_url.resolve(href->second)) {
								return url;
							}
						}
					}
				}
				elt = &*elt->parent.lock();
			}
		}
		return std::nullopt;
	}

	void resize(int new_width, int new_height, FontCache& font_cache) {
		m_width = new_width;
		m_height = new_height;
		m_document = std::make_shared<DocumentLayout>(m_nodes, m_width);
		m_document->layout(font_cache);
		m_display_list.clear();
		paint_tree(*m_document, m_display_list);
		clamp_scroll();

	}

	Tab(int width, int height)
		: m_width(width)
		, m_height(height)
		, m_default_style_sheet()
		, m_document()
		, m_display_list()
		, m_connection_manager()
	{
		StyleSheet default_style_sheet{};
		if (auto css_string = read_entire_file_to_string("styles/browser.css")) {
			std::optional<StyleSheet> default_css { CSSParser(*css_string).parse() };
			assert(default_css.has_value() && "invalid browser.css");
			m_default_style_sheet = *default_css;
		}
	}
};

class Browser;

class Chrome {
	int m_width;
	std::shared_ptr<SkFont> m_font;
	float m_font_height;
	float m_padding;
	float m_tabbar_top;
	float m_tabbar_bottom;
	float m_plus_width;
	SkRect m_newtab_rect;
	float m_urlbar_top;
	float m_urlbar_bottom;
	float m_bottom;
	float m_back_width;
	SkRect m_back_rect;
	float m_forward_width;
	SkRect m_forward_rect;
	SkRect m_address_rect;

	enum class Focus {
		Nothing,
		AddressBar,
	};

	Focus m_focus{Focus::Nothing};
	std::string m_address_bar{};

	// should i just define this all in browser or smth?
	friend class Browser;

public:
	// todo: handle resize (currently nothing reasonable comes to mind
	Chrome(FontCache& font_cache, int width) {
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

	SkRect tab_rect(int index) {
		auto tab_start = m_newtab_rect.fRight + m_padding;
		auto tab_width = m_font->measureText("Tab X", 5, SkTextEncoding::kUTF8) + 2 * m_padding;
		return SkRect::MakeLTRB(
			tab_start + tab_width * index, 
			m_tabbar_top,
			tab_start + tab_width * (index + 1), 
			m_tabbar_bottom);
	}

	void keypress(char c) {
		if (m_focus == Focus::AddressBar) {
			m_address_bar.push_back(c);
		}
	}

	void backspace() {
		if (m_focus == Focus::AddressBar && !m_address_bar.empty()) {
			m_address_bar.pop_back();
		}
	}

	void enter(Browser& browser);
	void click(Browser& browser, float x, float y);
	void paint(Browser const& browser, std::vector<std::shared_ptr<DrawCommand>>& commands);
};

enum class ClickType {
	Left,
	Middle,
	Right,
};

class Browser {
	int m_width;
	int m_height;
	SDL_Window *m_window;
	SDL_Renderer *m_renderer;
	SDL_Texture *m_texture;
	sk_sp<SkSurface> m_root_surface;
	SkImageInfo m_surface_info;
	sk_sp<SkFontMgr> m_font_mgr;
	FontCache m_font_cache;

	std::vector<std::unique_ptr<Tab>> m_tabs{};
	size_t m_active_tab{};
	Chrome m_chrome;

	friend class Chrome;

public:
	static std::optional<std::unique_ptr<Browser>> create() {
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

	void new_tab(URL url) {
		auto new_tab = std::make_unique<Tab>(m_width, m_height - m_chrome.m_bottom);
		new_tab->load(url, m_font_cache, true);
		m_tabs.push_back(std::move(new_tab));
		set_active_tab(m_tabs.size() - 1);
		draw();
	}

	void set_active_tab(size_t index) {
		m_active_tab = index;
		set_window_title();
	}

	void set_window_title() {
		if (auto title = m_tabs[m_active_tab]->title()) {
			SDL_SetWindowTitle(m_window, title->data());
		} else {
			SDL_SetWindowTitle(m_window, "Ladybug");
		}
	}

	void draw() {
		auto canvas = m_root_surface->getCanvas();
		canvas->clear(SK_ColorWHITE);

		m_tabs[m_active_tab]->draw(canvas, m_chrome.m_bottom);
		
		std::vector<std::shared_ptr<DrawCommand>> chrome_draws;
		m_chrome.paint(*this, chrome_draws);
		for (auto const& cmd : chrome_draws) {
			cmd->execute(0, *canvas);
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

	void resize(int new_width, int new_height) {
		m_width = new_width;
		m_height = new_height;
		SDL_DestroyTexture(m_texture);

		initialize_texture(m_renderer, m_width, m_height, m_texture, m_root_surface, m_surface_info);

		for (auto& tab : m_tabs) {
			tab->resize(new_width, new_height, m_font_cache);
		}
		draw();
	}

	void scroll_up() {
		m_tabs[m_active_tab]->scroll_up();
		draw();
	}

	void scroll_down() {
		m_tabs[m_active_tab]->scroll_down();
		draw();
	}

	void click(ClickType type, float x, float y) {
		if (y < m_chrome.m_bottom) {
			if (type == ClickType::Left) {
				m_chrome.click(*this, x, y);
			}
		} else {
			if (type != ClickType::Right) {
				float tab_y = y - m_chrome.m_bottom;
				auto url = m_tabs[m_active_tab]->click(x, tab_y);
				if (url) {
					if (type == ClickType::Left) {
						m_tabs[m_active_tab]->load(*url, m_font_cache, true);
					} else {
						new_tab(*url);
					}
					set_window_title();
				}
			}
		}
		draw();
	}

	void handle_key(SDL_KeyboardEvent event) {
		if (event.type == SDL_EVENT_KEY_UP) {
			return;
		}
		if (event.key == SDLK_DOWN) {
			scroll_down();
		} else if (event.key == SDLK_UP) {
			scroll_up();
		} else if (event.key == SDLK_RETURN) {
			m_chrome.enter(*this);
		} else if (event.key == SDLK_BACKSPACE) {
			m_chrome.backspace();
		} else if (event.key >= 0x20 && event.key < 0x7f) {
			char c = SDL_GetKeyFromScancode(event.scancode, event.mod, false);
			m_chrome.keypress(c);
		}
		draw();
	}

	void destroy() {
		SDL_DestroyTexture(m_texture);
		SDL_DestroyRenderer(m_renderer);
		SDL_DestroyWindow(m_window);
	}

	Browser(
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
};

void Chrome::enter(Browser& browser) {
	if (m_focus == Focus::AddressBar) {
		if (auto url = URL::create(m_address_bar)) {
			browser.m_tabs[browser.m_active_tab]->load(*url, browser.m_font_cache, true);
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
		bool navigated = browser.m_tabs[browser.m_active_tab]->go_back(browser.m_font_cache);
		if (navigated) {
			browser.set_window_title();
		}
	} else if (m_forward_rect.contains(x, y)) {
		bool navigated = browser.m_tabs[browser.m_active_tab]->go_forward(browser.m_font_cache);
		if (navigated) {
			browser.set_window_title();
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

int main(int argc, char** argv) {

	bool done = false;

	SDL_Init(SDL_INIT_VIDEO);

	auto b = Browser::create();
	if (!b.has_value()) {
		SDL_Quit();
		return 1;
	}
	auto browser = std::move(*b);

	network_init();

	URL url = [&] {
		if (argc == 1) {
			return URL::create("file:./test_data/index.html").value_or(URL::ABOUT_BLANK);
		} else if (argc == 2) {
			return URL::create(argv[1]).value_or(URL::ABOUT_BLANK);
		} else {
			assert(false && "Invalid arguments");
		}
	}();

	auto begin = std::chrono::steady_clock::now();
	browser->new_tab(url);
	auto end = std::chrono::steady_clock::now();
	std::cout << "Page loaded in " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms" << std::endl;

	while (!done) {
		SDL_Event event;

		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				done = true;
			} else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				int width = event.window.data1;
				int height = event.window.data2;
				browser->resize(width, height);
			} else if (event.type == SDL_EVENT_KEY_DOWN) {
				browser->handle_key(event.key);
			} else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
				// todo: smooth scrolling
				if (event.wheel.y > 0) {
					browser->scroll_up();
				} else if (event.wheel.y < 0) {
					browser->scroll_down();
				}
			} else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
				if (event.button.button == 1) {
					browser->click(ClickType::Left, event.button.x, event.button.y);
				} else if (event.button.button == 2) {
					browser->click(ClickType::Middle, event.button.x, event.button.y);
				} else if (event.button.button == 3) {
					browser->click(ClickType::Right, event.button.x, event.button.y);
				}
			}
		}

		SDL_Delay(16);
	}

	browser->destroy();

	SDL_Quit();

	return 0;
}
