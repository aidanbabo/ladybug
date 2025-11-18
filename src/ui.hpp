#pragma once
#include <SDL3/SDL.h>

#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <functional>

#include "draw.hpp"
#include "network.hpp"
#include "parsers.hpp"
#include "layout.hpp"

int const INITIAL_WIDTH  = 800;
int const INITIAL_HEIGHT = 600;
int const SCROLL_STEP = 100;

enum class HistoryNavigationAttempt {
	NoNavigation,
	NeedsConfirmation,
	Navigated,
};

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
	ConnectionManager m_connection_manager;
	int m_scroll = 0;
	URL m_url = URL::ABOUT_BLANK;
	std::shared_ptr<Element> m_focus;

public:
	void load(HttpRequest request, FontCache& font_cache, bool alter_history);

	std::optional<HttpRequest> submit_form(std::shared_ptr<Element> node);

	void render(FontCache& font_cache);
	
	void draw(SkCanvas *canvas, float offset);

	std::optional<std::string> title();

	URL const& url() const;

	void blur();

	bool can_go_back() const;

	HistoryNavigationAttempt go_back(FontCache& font_cache, bool force = false);

	bool can_go_forward() const;

	HistoryNavigationAttempt go_forward(FontCache& font_cache, bool force = false);

	void clamp_scroll();

	void scroll_up();

	void scroll_down();

	[[nodiscard]]
	std::optional<HttpRequest> keypress(SDL_KeyboardEvent event, FontCache& font_cache);

	// If there is navigation involved, a value is returned for the browser to navigate to.
	// todo: I think we're gettingo the point where passing around the font_cache is getting annoying.
	[[nodiscard]]
	std::optional<HttpRequest> click(float x, float y, FontCache& font_cache);

	void resize(int new_width, int new_height, FontCache& font_cache);

	Tab(int width, int height);
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
	Chrome(FontCache& font_cache, int width);

	SkRect tab_rect(int index);

	void keypress(char c);

	void backspace();

	void blur();

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
	SDL_Renderer *m_renderer;
	SDL_Texture *m_texture;
	sk_sp<SkSurface> m_root_surface;
	SkImageInfo m_surface_info;
	sk_sp<SkFontMgr> m_font_mgr;
	FontCache m_font_cache;

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

public:
	static std::optional<std::unique_ptr<Browser>> create();

	void new_tab(HttpRequest request);

	void set_active_tab(size_t index);

	void set_window_title();

	void draw();

	void resize(int new_width, int new_height);

	void scroll_up();

	void scroll_down();

	void handle_click(ClickType type, float x, float y);

	void handle_key(SDL_KeyboardEvent event);

	void navigation_confirmation_popup(bool going_back);

	void destroy();

	Browser(
		int width,
		int height,
		SDL_Window *window,
		SDL_Renderer *renderer,
		SDL_Texture *texture,
		sk_sp<SkSurface> root_surface,
		SkImageInfo surface_info,
		sk_sp<SkFontMgr> font_mgr,
		FontCache font_cache);
	~Browser();
};
