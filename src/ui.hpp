#pragma once
#include <SDL3/SDL.h>

#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <functional>
#include <unordered_set>

#include "draw.hpp"
#include "network.hpp"
#include "parsers.hpp"
#include "layout.hpp"
#include "jscontext.hpp"
#include "task_runner.hpp"

int const INITIAL_WIDTH  = 800;
int const INITIAL_HEIGHT = 600;
int const SCROLL_STEP = 100;

enum class HistoryNavigationAttempt {
	NoNavigation,
	NeedsConfirmation,
	Navigated,
};

class Browser;

class Tab {
	int m_width;
	int m_height;
	StyleSheet m_default_style_sheet;
	std::shared_ptr<Node> m_nodes{};
	StyleSheet m_rules{};
	std::shared_ptr<DocumentLayout> m_document{};
	std::vector<std::shared_ptr<DrawCommand>> m_display_list{};
	std::vector<HttpRequest> m_history{};
	size_t m_history_index = -1;
	int m_scroll = 0;
	std::optional<URL> m_url = std::nullopt;
	std::shared_ptr<Element> m_focus;
	std::optional<JSContext> m_js;
	Browser& m_browser;
	// URL.origin()
	std::optional<std::unordered_set<std::string>> m_allowed_origins;
	TaskRunner m_task_runner;


	friend class JSContext;

public:
	void load(HttpRequest request, bool alter_history);

	std::optional<HttpRequest> submit_form(std::shared_ptr<Element> node);

	void render();
	
	void raster(SkCanvas *canvas);

	std::optional<std::string> title();

	URL const& url() const;
	float document_height() const;
	float scroll() const;

	bool allowed_request(URL const& url) const;

	void blur();

	bool can_go_back() const;

	HistoryNavigationAttempt go_back(bool force = false);

	bool can_go_forward() const;

	HistoryNavigationAttempt go_forward(bool force = false);

	void clamp_scroll();

	void scroll_up();

	void scroll_down();

	void run_task();

	[[nodiscard]]
	std::optional<HttpRequest> keypress(SDL_KeyboardEvent event);

	[[nodiscard]]
	std::optional<HttpRequest> click(float x, float y);

	void resize(int new_width, int new_height);

	Tab(int width, int height, Browser& browser);
};

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
	Chrome(FontCache& font_cache, int width);

	SkRect tab_rect(int index);

	void keypress(char c);

	void backspace();

	void blur();
	float bottom() const;

	void enter(Browser& browser);
	void click(Browser& browser, float x, float y);
	void paint(Browser const& browser, std::vector<std::shared_ptr<DrawCommand>>& commands);
};

class PopUp {
	std::shared_ptr<SkFont> m_font;
	std::vector<std::string> m_prompts;
	std::vector<std::string> m_options;
	std::vector<float> m_prompts_widths;
	std::vector<SkRect> m_options_rects;
	std::vector<std::function<void(Browser&)>> m_actions;
	float m_padding;
	float m_option_padding;
	float m_top;
	float m_left;
	float m_width;
	float m_height;

	float m_font_height;
	SkRect m_rect;
public:
	PopUp(FontCache& font_cache, int width, std::vector<std::string> prompts, std::vector<std::string> options, std::vector<std::function<void(Browser &)>> actions);

	void click(Browser& browser, float x, float y);
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands);
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
	sk_sp<SkSurface> m_root_surface;
	sk_sp<SkSurface> m_chrome_surface;
	sk_sp<SkSurface> m_tab_surface;
	// todo: popup surface?
	// sk_sp<SkSurface> m_popup_surface;
	sk_sp<SkFontMgr> m_font_mgr;
	FontCache m_font_cache;
	ConnectionManager m_connection_manager;

	std::vector<std::unique_ptr<Tab>> m_tabs{};
	size_t m_active_tab{};
	Chrome m_chrome;
	std::optional<PopUp> m_popup;

	enum class Focus {
		Nothing,
		Alert,
		Chrome,
		Content,
	};
	Focus m_focus{Focus::Nothing};

	friend class Chrome;
	friend class Alert;
	friend class Tab;
	friend class JSContext;

public:
	static std::optional<std::unique_ptr<Browser>> create();

	void new_tab(HttpRequest request);

	void set_active_tab(size_t index);

	void set_window_title();

	void raster_chrome();
	void raster_tab();
	void draw();

	void resize(int new_width, int new_height);

	void scroll_up();
	void scroll_down();

	void handle_click(ClickType type, float x, float y);
	void handle_key(SDL_KeyboardEvent event);

	void navigation_confirmation_popup(bool going_back);

	void run_task();

	void destroy();

	Browser(
		int width,
		int height,
		SDL_Window *window,
		sk_sp<SkSurface> root_surface,
		sk_sp<SkSurface> chrome_surface,
		sk_sp<SkSurface> tab_surface,
		sk_sp<SkFontMgr> font_mgr,
		FontCache font_cache,
		Chrome chrome);
	~Browser();
};
