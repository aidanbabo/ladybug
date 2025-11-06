// todo: Exercise 2-5: Emoji -> done with text stuff for now
// todo: Exercise 3-4: Small caps -> done with text stuff for now
// todo: Exercise 3-5: Preformatted text -> done with text stuff for now
// todo: Exercise 4-5: Syntax highlighting -> assuming CSS will make the implementation better/not interested
// todo: Exercise 4-6: Mis-nested formatting tags -> assuming we get to it a little better later
// todo: Exercise 5-4: Table of contents -> seems like not a real thing? maybe a real use case will appear later on?
// todo: Exercise 5-6: Run-ins -> Browser support is already poor and the feature is too niche

// todo: reimplement <sup> (and then maybe <sub>?) now that we are using stylesheets instead of code

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

#include <iostream>

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
#include "parsers.hpp"

void initialize_texture(SDL_Renderer *renderer, int width, int height, SDL_Texture *&texture, sk_sp<SkSurface> &root_surface, SkImageInfo &info) {
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, width, height);

	info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
	SkSurfaceProps surface_props;
	size_t row_bytes = width * 4;
	root_surface = SkSurfaces::Raster(info, row_bytes, &surface_props);
	assert(root_surface);
}

void tree_to_list(std::shared_ptr<Node> node, std::vector<std::shared_ptr<Node>>& list) {
	list.push_back(node);
	for (auto const& c : node->children) {
		tree_to_list(c, list);
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

class Browser {
	SDL_Window *m_window;
	SDL_Renderer *m_renderer;
	SDL_Texture *m_texture;
	sk_sp<SkSurface> m_root_surface;
	SkImageInfo m_surface_info;
	sk_sp<SkFontMgr> m_font_mgr;
	std::shared_ptr<Node> m_nodes;
	std::shared_ptr<DocumentLayout> m_layout;
	std::vector<std::shared_ptr<DrawCommand>> m_display_list;
	FontCache m_font_cache;
	StyleSheet m_default_style_sheet;

	int m_scroll = 0;
	int m_width = INITIAL_WIDTH;
	int m_height = INITIAL_HEIGHT;

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

		StyleSheet default_style_sheet{};
		if (auto css_string = read_entire_file_to_string("styles/browser.css")) {
			std::optional<StyleSheet> default_css { CSSParser(*css_string).parse() };
			assert(default_css.has_value() && "invalid browser.css");
			default_style_sheet = *default_css;
		}

		return Browser(window, renderer, texture, root_surface, info, font_mgr, font_cache, default_style_sheet);
	}

	void load(ConnectionManager& cm, URL url) {
		std::string body = cm.request(url);
		m_nodes = HTMLParser(body).parse();
		assert(m_nodes->type == NodeType::Element);
		// todo: This is supposed to be a copy (from reference) does this happen?
		StyleSheet rules = m_default_style_sheet;
		std::vector<std::shared_ptr<Node>> nodes;
		tree_to_list(m_nodes, nodes);
		std::vector<std::string> links;
		for (auto const& node : nodes) {
			if (node->type != NodeType::Element) {
				continue;
			}
			auto element = static_cast<Element const&>(*node);
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
			links.push_back(element.attributes["href"]);
		}

		for (auto const& link : links) {
			auto style_url = url.resolve(link);
			if (!style_url) {
				std::cerr << "Skipping malformed url: '" << link << "'" << std::endl;
				continue;
			}

			// todo: handle errors fr
			auto body = cm.request(*style_url);
			auto sh = CSSParser(body).parse();
			if (!sh) {
				std::cerr << "Improper stylesheet at: '" << link << "'" << std::endl;
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
		m_layout = std::make_shared<DocumentLayout>(m_nodes);
		m_layout->layout(m_font_cache, m_width);
		//m_layout->print_layout();
		m_display_list.clear();
		paint_tree(*m_layout, m_display_list);
		draw();
	}
	
	void draw() {
		SkPaint paint;
		paint.setColor(SK_ColorBLACK);

		auto canvas = m_root_surface->getCanvas();
		canvas->clear(SK_ColorWHITE);

		// content
		for (auto command : m_display_list) {
			if (command->top > m_scroll + m_height) continue;
			if (command->bottom < m_scroll) continue;
			command->execute(m_scroll, *canvas);
		}

		// scrollbar
		if (m_height < m_layout->m_height) {
			float scrollbar_ratio = (float) m_height / (float) m_layout->m_height;
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
		// todo: recalculate sroll so we stay at the same height
		m_width = new_width;
		m_height = new_height;
		SDL_DestroyTexture(m_texture);

		initialize_texture(m_renderer, m_width, m_height, m_texture, m_root_surface, m_surface_info);

		m_layout = std::make_shared<DocumentLayout>(m_nodes);
		m_layout->layout(m_font_cache, m_width);
		m_display_list.clear();
		paint_tree(*m_layout, m_display_list);
		clamp_scroll();
		draw();
	}

	void clamp_scroll() {
		if (m_scroll < 0) {
			m_scroll = 0;
		}

		int max_scroll = m_layout->m_height - m_height;
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
		FontCache font_cache,
		StyleSheet sheet
	)
		: m_window(window)
		, m_renderer(renderer)
		, m_texture(texture)
		, m_root_surface(root_surface)
		, m_surface_info(surface_info)
		, m_font_mgr(font_mgr)
		, m_layout()
		, m_display_list()
		, m_font_cache(font_cache)
		, m_default_style_sheet(sheet)
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
	Browser browser { b.value() };

	network_init();

	auto connection_manager = ConnectionManager();
	URL url = [&] {
		if (argc == 1) {
			return URL::create("file:./test_data/index.html").value_or(URL::ABOUT_BLANK);
		} else if (argc == 2) {
			return URL::create(argv[1]).value_or(URL::ABOUT_BLANK);
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
