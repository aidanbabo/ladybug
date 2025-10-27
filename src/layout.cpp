#include "include/core/SkFontTypes.h"
#include "include/core/SkFontMetrics.h"

#include "utils.hpp"
#include "layout.hpp"

#include <cassert>

#include <iostream>
#include <numeric>


// lowkey hate implicit mutable references, but pairs also suck...
void unescape_sequence(std::string_view body, size_t &i, std::string &out) {
	// todo: allow semicolons or no semicolons
	// todo: trie?, at least use an array of pairs if this gets too long
	if (body.compare(i, 3, "lt;") == 0) {
		i += 3;
		out.push_back('<');
	} else if (body.compare(i, 3, "gt;") == 0) {
		i += 3;
		out.push_back('>');
	} else if (body.compare(i, 4, "amp;") == 0) {
		i += 4;
		out.push_back('&');
	} else if (body.compare(i, 5, "quot;") == 0) {
		i += 5;
		out.push_back('"');
	} else if (body.compare(i, 3, "shy") == 0) {
		i += 3;
		out += SOFT_HYPHEN;
	} else {
		out.push_back('&');
	}
}

// todo: handle nesting tags...
std::vector<Token> lex(std::string_view body) {
	std::vector<Token> out;
	std::string buffer;
	size_t i = 0;
	bool in_tag = false;
	while (i < body.length()) {
		char c = body[i++];
		if (c == '<') {
			in_tag = true;
			if (!buffer.empty()) {
				out.push_back(Token {
					.tag = TokenTag::Text,
					.data = buffer,
				});
				buffer = "";
			}
		} else if (c == '>') {
			in_tag = false;
			if (!buffer.empty()) {
				out.push_back(Token {
					.tag = TokenTag::Tag,
					.data = buffer,
				});
				buffer = "";
			}
		} else if (!in_tag && c == '&') {
			unescape_sequence(body, i, buffer);
		} else { 
			buffer.push_back(c);
		}
	}

	if (!in_tag && !buffer.empty()) {
		out.push_back(Token {
			.tag = TokenTag::Text,
			.data = buffer,
		});
	}
	return out;
}

bool FontInfo::operator==(const FontInfo& other) const noexcept {
	return size == other.size && bold == other.bold && italic == other.italic;
}

size_t std::hash<FontInfo>::operator()(const FontInfo& f) const noexcept {
	size_t seed = 0;
	combine_hash(seed, std::hash<size_t>{}(f.size));
	combine_hash(seed, std::hash<bool>{}(f.bold));
	combine_hash(seed, std::hash<bool>{}(f.italic));
	return seed;
}

FontCache::FontCache(FontType ty) : m_font_type(ty) {}

SkFont& FontCache::get_font(size_t size, bool bold, bool italic) {
	auto info = FontInfo {
		.size = size,
		.bold = bold,
		.italic = italic,
	};
	if (auto f = m_fonts.find(info); f != m_fonts.end()) {
		return f->second;
	}

	sk_sp<SkTypeface> typeface = [&] {
		if (!bold && !italic) {
			return m_font_type.normal;
		} else if (bold && !italic) {
			return m_font_type.bold;
		} else if (!bold && italic) {
			return m_font_type.italic;
		} else if (bold && italic) {
			return m_font_type.bold_italic;
		} else {
			assert(false && "unreachable");
		}
	}();
	SkFont font(typeface, size);
	m_fonts.insert({info, font});
	return m_fonts[info];
}

// this approach is odd...
Layout::Layout(std::vector<Token> tokens, FontCache &font_cache, int width, bool right_align) {
	m_width = width;
	m_right_align = right_align;

	for (Token const& tok : tokens) {
		token(tok, font_cache);
	}

	flush();
}

ComputedLayout Layout::computed() const {
	return {
		.display_list = std::move(m_display_list),
		.must_render_up_to_y = m_must_render_up_to_y,
	};
}

void Layout::token(Token const& tok, FontCache &font_cache) {
	if (tok.tag == TokenTag::Text) {

		SkFont &font = font_cache.get_font(m_size, m_is_bold, m_is_italic);
		auto words = split_on_any(tok.data, " \r\n\t");
		for (auto const &w : words) {
			auto w2 = trim_whitespace(w);
			if (w2 != "") {
				word(w2, font);
			}
		}
	} else if (tok.tag == TokenTag::Tag) {
		if (tok.data == "i") {
			m_is_italic = true;
		} else if (tok.data == "/i") {
			m_is_italic = false;
		} else if (tok.data == "b") {
			m_is_bold = true;
		} else if (tok.data == "/b") {
			m_is_bold = false;
		} else if (tok.data == "small") {
			m_size -= 2;
		} else if (tok.data == "/small") {
			m_size += 2;
		} else if (tok.data == "big") {
			m_size += 4;
		} else if (tok.data == "/big") {
			m_size -= 4;
		} else if (tok.data == "br") {
			flush();
		} else if (tok.data == "/p") {
			flush();
			m_cursor_y += VSTEP;
		// todo: remove. this is non-standard
		} else if (tok.data == "h1 class=\"title\"") {
			flush();
			m_in_title = true;
		} else if (tok.data == "/h1") {
			flush();
			m_in_title = false;
		} else if (tok.data == "sup") {
			m_in_sup = true;
			// todo: halving text looks stupid, but it is probably an ascent related issue 
			m_size -= 2;
		} else if (tok.data == "/sup") {
			m_in_sup = false;
			m_size += 2;
		} else {
			// do nothing
		}
	} else {
		assert(false && "unreachable");
	}
}

// todo: cleanup with fresh eyes
void Layout::word(std::string const &word, SkFont &font) {
	// The algorithm is to split a word into its separable parts and to try and render all of the parts at once.
	// If this fails we remove the last separable part and try again.
	//   We keep track of these parts to be rendered later (on a new line).
	// If we have no separable parts then we need to flush and start over.
	std::vector<std::string> soft_hyphen_split = split(word, SOFT_HYPHEN);
	std::vector<std::string> hyphens_to_render_later;

	size_t failthrough;
	size_t const MAX_FAILTHROUGH = 1000;
	for (
		failthrough = 0;
		!soft_hyphen_split.empty() && failthrough < MAX_FAILTHROUGH;
		failthrough++
	) {
		std::string word_to_render = std::accumulate(soft_hyphen_split.begin(), soft_hyphen_split.end(), std::string(""), [](std::string a, std::string b) { return a + b; });
		if (!hyphens_to_render_later.empty()) {
			word_to_render += "-";
		}
		auto text_width_max = m_width - HSTEP - m_cursor_x;
		auto w = font.measureText(word_to_render.c_str(), word_to_render.size(), SkTextEncoding::kUTF8);

		if (w < text_width_max || (m_cursor_x == HSTEP && soft_hyphen_split.size() == 1)) {
			// If the word fits or will never fit at the current resolution we render it!
			auto pos = StringPosition {
				.x = m_cursor_x,
				.y = -1, // filled in during `flush`
				.width = w,
				.string = word_to_render,
				.font = font,
				.is_super_text = m_in_sup,
			};
			m_line.push_back(pos);

			// todo: other text encodings
			m_cursor_x += w + font.measureText(" ", 1, SkTextEncoding::kUTF8);

			// Optimization: If there is more work, it is because this line is full, so we must flush anyway!
			// This prevents us from trying to split the remaining string over and over when none of it will fit.
			if (!hyphens_to_render_later.empty()) {
				flush();
			}
			// When there are no leftovers here we will exit the loop.
			soft_hyphen_split = hyphens_to_render_later;
			hyphens_to_render_later = {};
		} else if (soft_hyphen_split.size() > 1) {
			// We can split the text based on hyphens, so we do so, noting we have to come back to the extras later.
			std::string v = *soft_hyphen_split.erase(soft_hyphen_split.end() - 1);
			hyphens_to_render_later.insert(hyphens_to_render_later.begin(), v);
		} else {
			// The word didn't fit and we couldn't split it, so we flush and try the next line.
			flush();
			soft_hyphen_split.insert(soft_hyphen_split.end(), hyphens_to_render_later.begin(), hyphens_to_render_later.end());
			hyphens_to_render_later = {};
		}
	}

	if (failthrough == MAX_FAILTHROUGH) {
		std::cerr << "Failed to render text: " << word << std::endl;
	}
}

void Layout::flush() {
	if (m_line.empty()) {
		return;
	}

	// todo: cool function for this?
	std::vector<SkFontMetrics> metrics;
	for (auto const& pos : m_line) {
		SkFontMetrics m;
		pos.font.getMetrics(&m);
		metrics.push_back(m);
	}
	// todo: cool function for this!
	// ascent in skia is typically a negative number, but for Tk it's positive...
	float max_ascent = -100000;
	for (auto const& m : metrics) {
		if (-m.fAscent > max_ascent) {
			max_ascent = -m.fAscent;
		}
	}
	// todo: metrics.fLeading
	float baseline = m_cursor_y + max_ascent * 1.25;

	// todo: zip?
	for (size_t i = 0; i < m_line.size(); i++) {
		if (m_line[i].is_super_text) {
			m_line[i].y = baseline - max_ascent / 2;
		} else {
			// apparently, Skia draws text from the baseline, not from the NW
			m_line[i].y = baseline;
		}
	}

	if (m_in_title || m_right_align) {
		auto const& w = m_line[m_line.size() - 1];
		// todo: put in StringPosition?
		float right_side_gap = (float) m_width - w.x - w.width - (float) HSTEP;

		float change = (m_in_title) ? right_side_gap / 2 : right_side_gap;
		for (auto &pos : m_line) {
			pos.x += change;
		}
	}

	for (auto pos : m_line) {
		m_display_list.push_back(std::move(pos));
	}

	// todo: cool function for this!
	float max_descent = -100000;
	for (auto const& m : metrics) {
		if (m.fDescent > max_descent) {
			max_descent = m.fDescent;
		}
	}

	// todo: metrics.fLeading
	m_cursor_y = baseline + max_descent * 1.25;
	m_cursor_x = HSTEP;
	m_line = {};

	if (m_cursor_y + VSTEP > m_must_render_up_to_y) {
		m_must_render_up_to_y = m_cursor_y + VSTEP;
	}
}
