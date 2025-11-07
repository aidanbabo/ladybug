# ladybug

This is a project to help me learn web browser development and better know C++.

It may be a little ugly, but it's honest work.

## Build
System dependencies: `zlib`, `libssl`, `libcrypto`.

- Run `vcpkg install` to install dependencies.
- Run `cmake --preset default` to configure the build.
- Run `cmake --build --preset default` to build the project.

## Running
- Run `./build/ladybird [url]` to run the browser.
    - The URL must contain the scheme (currently, `http`, `https`, `file` or `data`).
- Providing no arguments will default it to `./test_data/index.html`.

## Source
This browsers follows along with the tutorial at [https://browser.engineering](). This resource was recommended by the fine fellows who write the Ladybird browser (hence the name ladybug) to understand how a browser works.

The book's browser is written in Python, initially with Tkinter and later with Skia and SDL. I'm writing it in C++20 (unless C++23 turns out to be much better) with Skia and SDL3. If you're seeing a little bit of awkwardness with rendering and ignored errors, there is a chance I'm waiting for the tutorials to reach the point where they use Skia as well so that code can match up better. A small chance ;-;.

Some of the exercises have been done, only the ones I find technically interesting or are huge usability improvements.

## Capabilities
- Fetch an HTML document over HTTP or HTTPS.
    - Supports gzip and deflate encodings as well as chunked transfer.
    - Has a barebones HTTP cache and keep alive connections.
- Support for absolute and relative `file:` urls and `data:text/html,` urls.
- Renders HTML content to the screen and styles it with CSS.
    - Loads CSS from remote sources and implements the cascade.
    - Support for most font properties and some layout properties.
    - Supports tag, class and descendant selectors as well as selector sequences (i.e. `tag.class`).
    - Supports basic text styling with `<b>`, `<i>`, `<sup>` and others via the user agent style sheet.
- Scrolling and resizing the window
    - Scrolling via the mousewheel is a little chunky. My mouse wheel doesn't work so testing is difficult. Use the arrow keys.
