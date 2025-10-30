// todo: Exercise 2-5: Emoji -> done with text stuff for now
// todo: Exercise 3-4: Small caps -> done with text stuff for now
// todo: Exercise 3-5: Small caps -> done with text stuff for now

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// Skia
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_directory.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <zlib.h>

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include <optional>
#include <string>
#include <vector>

int const INITIAL_WIDTH  = 800;
int const INITIAL_HEIGHT = 600;
int const SCROLL_STEP = 100;

#include "layout.hpp"
#include "network.hpp"
#include "utils.hpp"

void initialize_texture(SDL_Renderer *renderer, int width, int height, SDL_Texture *&texture, sk_sp<SkSurface> &root_surface, SkImageInfo &info) {
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, width, height);

	info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
	SkSurfaceProps surface_props;
	size_t row_bytes = width * 4;
	root_surface = SkSurfaces::Raster(info, row_bytes, &surface_props);
	assert(root_surface);
}


class Browser {
	SDL_Window *m_window;
	SDL_Renderer *m_renderer;
	SDL_Texture *m_texture;
	sk_sp<SkSurface> m_root_surface;
	SkImageInfo m_surface_info;
	sk_sp<SkFontMgr> m_font_mgr;
	std::vector<Token> m_tokens;
	ComputedLayout m_layout;
	FontCache m_font_cache;

	int m_scroll = 0;
	int m_width = INITIAL_WIDTH;
	int m_height = INITIAL_HEIGHT;
	// todo: make a cli arg! find a library for this!
	bool m_right_align = false;

public:
	static std::optional<Browser> create() {
		// todo: change to OpenGL
		SDL_Window *window = SDL_CreateWindow("Ladybug", INITIAL_WIDTH, INITIAL_HEIGHT, SDL_WINDOW_RESIZABLE);

		if (window == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create window: %s\n", SDL_GetError());
			return std::nullopt;
		}

		SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);

		SDL_Texture *texture;
		SkImageInfo info;
		sk_sp<SkSurface> root_surface;
		initialize_texture(renderer, INITIAL_WIDTH, INITIAL_HEIGHT, texture, root_surface, info);

		// this doesn't ever want to seem to work so full paths it is
		sk_sp<SkFontMgr> font_mgr = SkFontMgr_New_Custom_Directory("/home/ababo/dev/browser/fonts");
		assert(font_mgr);
		sk_sp<SkTypeface> normal      = font_mgr->makeFromFile("/home/ababo/dev/browser/fonts/Times New Roman.ttf");
		sk_sp<SkTypeface> bold        = font_mgr->makeFromFile("/home/ababo/dev/browser/fonts/Times New Roman Bold.ttf");
		sk_sp<SkTypeface> italic      = font_mgr->makeFromFile("/home/ababo/dev/browser/fonts/Times New Roman Italic.ttf");
		sk_sp<SkTypeface> bold_italic = font_mgr->makeFromFile("/home/ababo/dev/browser/fonts/Times New Roman Bold Italic.ttf");
		assert(normal);
		assert(bold);
		assert(italic);
		assert(bold_italic);

		FontType times_new_roman = FontType {
			.normal = normal,
			.bold = bold,
			.italic = italic,
			.bold_italic = bold_italic,
		};

		FontCache font_cache = FontCache(times_new_roman);

		return Browser(window, renderer, texture, root_surface, info, font_mgr, font_cache);
	}

	void load(ConnectionManager& cm, URL url) {
		std::string body = cm.request(url);
		m_tokens = lex(body);
		m_layout = Layout(m_tokens, m_font_cache, m_width, m_right_align).computed();
		draw();
	}
	
	void draw() {
		SkPaint paint;
		paint.setColor(SK_ColorBLACK);

		auto canvas = m_root_surface->getCanvas();
		canvas->clear(SK_ColorWHITE);

		// content
		for (auto cpos : m_layout.display_list) {
			// todo: adding VSTEP is a crutch? idk why it doesn't work without it
			if (cpos.y > m_scroll + m_height + VSTEP) continue;
			if (cpos.y + VSTEP < m_scroll) continue;
			canvas->drawString(cpos.string.c_str(), cpos.x, cpos.y - m_scroll, cpos.font, paint);
		}

		// scrollbar
		if (m_height < m_layout.must_render_up_to_y) {
			float scrollbar_ratio = (float) m_height / (float) m_layout.must_render_up_to_y;
			float scrollbar_size = scrollbar_ratio * (float) m_height;
			float scrollbar_start = scrollbar_ratio * (float) m_scroll;
			paint.setColor(SK_ColorBLUE);
			canvas->drawRect(SkRect::MakeLTRB(m_width - HSTEP, scrollbar_start, m_width, scrollbar_start + scrollbar_size), paint);
		}

		sk_sp<SkImage> image = m_root_surface->makeImageSnapshot();

		size_t row_bytes = m_width * 4;

		std::vector<uint8_t> pixels(m_height * m_width * 4);
		if (!image->readPixels(m_surface_info, pixels.data(), row_bytes, 0, 0)) {
			// todo: error
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

		m_layout = Layout(m_tokens, m_font_cache, m_width, m_right_align).computed();
		// todo: do one better, make the scroll proportional to the new screen size
		// either by making m_scroll a float, or by recalculating it on resize.
		clamp_scroll();
		draw();
	}

	void clamp_scroll() {
		if (m_scroll < 0) {
			m_scroll = 0;
		}

		int max_scroll = m_layout.must_render_up_to_y - m_height;
		if (max_scroll < 0) {
			m_scroll = 0;
		} else if (m_scroll > max_scroll) {
			m_scroll = max_scroll;
		}
	}

	void scroll_up() {
		m_scroll -= SCROLL_STEP;
		clamp_scroll();
		draw();
	}

	void scroll_down() {
		m_scroll += SCROLL_STEP;
		clamp_scroll();
		draw();
	}

	void destroy() {
		SDL_DestroyTexture(m_texture);
		SDL_DestroyRenderer(m_renderer);
		SDL_DestroyWindow(m_window);
	}

private:
	Browser(
		SDL_Window *window,
		SDL_Renderer *renderer,
		SDL_Texture *texture,
		sk_sp<SkSurface> root_surface,
		SkImageInfo surface_info,
		sk_sp<SkFontMgr> font_mgr,
		FontCache font_cache
	)
		: m_window(window)
		, m_renderer(renderer)
		, m_texture(texture)
		, m_root_surface(root_surface)
		, m_surface_info(surface_info)
		, m_font_mgr(font_mgr)
		, m_layout()
		, m_font_cache(font_cache)
	{}
};

int main(int argc, char** argv) {
	bool done = false;

	SDL_Init(SDL_INIT_VIDEO);

	auto b = Browser::create();
	if (!b.has_value()) {
		SDL_Quit();
		return 1;
	}
	Browser browser = b.value();

	network_init();

	auto connection_manager = ConnectionManager();
	URL url = [&] {
		// todo: how to make value_or lazy
		if (argc == 1) {
			return URL::create("file:///home/ababo/dev/browser/test_data/index.html").value_or(URL::create("about:blank").value());
		} else if (argc == 2) {
			return URL::create(argv[1]).value_or(URL::create("about:blank").value());
		} else {
			assert(false && "Invalid arguments");
		}
	}();

	browser.load(connection_manager, url);

	while (!done) {
		SDL_Event event;

		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				done = true;
			} else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				int width = event.window.data1;
				int height = event.window.data2;
				browser.resize(width, height);
			} else if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_DOWN) {
					browser.scroll_down();
				} else if (event.key.key == SDLK_UP) {
					browser.scroll_up();
				}
			} else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
				// todo: smooth scrolling
				if (event.wheel.y > 0) {
					browser.scroll_up();
				} else if (event.wheel.y < 0) {
					browser.scroll_down();
				}
			}
		}

		SDL_Delay(16);
	}

	browser.destroy();

	SDL_Quit();

	return 0;
}
