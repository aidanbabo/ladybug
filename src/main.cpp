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
//
// Many exercises are being skipped because it makes the refactors in later chapters much harder. Chapter 7 was a pain to get done.
//
// todo: Exercise 8-4: Check boxes -> Bar is getting pretty low for me to skip these huh
// todo: Exercise 8-6: Message board -> I don't care about the server
// todo: Exercise 8-7: Persistence -> I don't care about the server
// todo: Exercise 8-8: Rich buttons -> This one actually seems kind of cool and should be one of the first I come back to after completion
// todo: Exercise 8-9: HTML chrome -> I am choosing to skip this because I think it's sort of a bad idea

// todo: reimplement <sup> (and then maybe <sub>?) now that we are using stylesheets instead of code

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>

#include "ui.hpp"
#include "network.hpp"

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
					browser->handle_click(ClickType::Left, event.button.x, event.button.y);
				} else if (event.button.button == 2) {
					browser->handle_click(ClickType::Middle, event.button.x, event.button.y);
				} else if (event.button.button == 3) {
					browser->handle_click(ClickType::Right, event.button.x, event.button.y);
				}
			}
		}

		SDL_Delay(16);
	}

	browser->destroy();

	SDL_Quit();

	return 0;
}
