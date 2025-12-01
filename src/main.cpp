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
// todo: Exercise 9-4: IDs -> This is a crazy thing to be standard! It might speed up scripts a little bit but I'll skip it for now
// todo: Exercise 9-6: Serializing HTML -> Coolish? Not very useful in my day to day
// todo: Exercise 9-7: Script-added scripts and style sheets -> I want to add this later for sure I am just itching to get to the part where we use Skia for real
// todo: Exercise 10-2: Certificate errors -> Networking needs an overhaul I'm putting off
// todo: Exercise 10-3: Script access -> I really wanna get to Skia! (and JS functions are super annoying rn!)
// todo: Exercise 10-4: Cookie Expiration -> Networking needs an overhaul I'm putting off
// todo: Exercise 10-5: Cross-origin resource sharing (CORS) -> Networking needs an overhaul I'm putting off
// todo: Exercise 10-6: Referer -> I think this is a bad idea! I may implement it so the default behavior is no referrer, but also network stack yatta yatta

// todo: reimplement <sup> (and then maybe <sub>?) now that we are using stylesheets instead of code
// todo: fixme: our preventDefault implementation also prevents further proper propagation in the event of navigation

// todo: split into more files. (incremental) build time is wild in this language

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

void mainloop(std::unique_ptr<Browser> browser) {
	for (;;) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				browser->destroy();
				SDL_Quit();
				return;
			} else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				int width = event.window.data1;
				int height = event.window.data2;
				browser->resize(width, height);
			} else if (event.type == SDL_EVENT_KEY_DOWN) {
				// todo: ? They use SDL_EVENT_TEXTINPUT here, which we have to opt in to in SDL3.
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
}

int main(int argc, char** argv) {
	URL url = [&] {
		if (argc == 1) {
			return URL::create("file:./test_data/index.html").value_or(URL::ABOUT_BLANK);
		} else if (argc == 2) {
			return URL::create(argv[1]).value_or(URL::ABOUT_BLANK);
		} else {
			assert(false && "Invalid arguments");
		}
	}();

	SDL_Init(SDL_INIT_VIDEO);
	// todo: look into SDL_SetAppMetadata

	auto b = Browser::create();
	if (!b.has_value()) {
		SDL_Quit();
		return 1;
	}
	auto browser = std::move(*b);

	network_init();

	auto begin = std::chrono::steady_clock::now();
	browser->new_tab(url);
	auto end = std::chrono::steady_clock::now();
	std::cout << "Page loaded in " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms" << std::endl;

	mainloop(std::move(browser));
	return 0;
}
