#include "include/core/SkFontTypes.h"
#include "include/core/SkFontMetrics.h"

#include "utils.hpp"
#include "layout.hpp"

#include <cassert>

#include <iostream>
#include <ranges>

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

// Class based approach is super OO, but it's fine.
// I guess I would just use a struct and not make them member functions... sooo... nbd
Layout::Layout(Node const& root, FontCache& font_cache, int width, bool right_align) {
	m_width = width;
	m_right_align = right_align;

	recurse(root, font_cache);

	flush();
}

ComputedLayout Layout::computed() const {
	return {
		.display_list = std::move(m_display_list),
		.must_render_up_to_y = m_must_render_up_to_y,
	};
}

void Layout::recurse(Node const& node, FontCache& font_cache) {
	if (node.type == NodeType::Text) {
		Text const& text = static_cast<Text const&>(node);

		SkFont &font = font_cache.get_font(m_size, m_is_bold, m_is_italic);
		std::vector<std::string_view> words = split_on_any(text.text);
		for (auto w : words) {
			w = trim_whitespace(w);
			if (w != "") {
				word(w, font);
			}
		}
	} else if (node.type == NodeType::Tag) {
		Tag const& tag = static_cast<Tag const&>(node);
		open_tag(tag);
		for (auto const& child : node.children) {
			recurse(*child, font_cache);
		}
		close_tag(tag);
	} else {
		assert(false && "unreachable");
	}
}

void Layout::open_tag(Tag const& tag) {
	if (tag.tag == "i") {
		m_is_italic = true;
	} else if (tag.tag == "b") {
		m_is_bold = true;
	} else if (tag.tag == "small") {
		m_size -= 2;
	} else if (tag.tag == "big") {
		m_size += 4;
	} else if (tag.tag == "br") {
		flush();
	} else if (tag.tag == "p") {
	} else if (tag.tag == "h1") {
		// todo: remove. this is non-standard
		if (auto f = tag.attributes.find("class"); f != tag.attributes.end() && f->second == "title") {
			flush();
			m_in_title = true;
		}
	} else if (tag.tag == "sup") {
		m_in_sup = true;
		// todo: halving text looks stupid, but it is probably an ascent related issue 
		m_size -= 2;
	}
	// no more recognized tags
}
void Layout::close_tag(Tag const& tag) {
	if (tag.tag == "i") {
		m_is_italic = false;
	} else if (tag.tag == "b") {
		m_is_bold = false;
	} else if (tag.tag == "small") {
		m_size += 2;
	} else if (tag.tag == "big") {
		m_size -= 4;
	} else if (tag.tag == "p") {
		flush();
		m_cursor_y += VSTEP;
	} else if (tag.tag == "h1") {
		flush();
		m_in_title = false;
	} else if (tag.tag == "sup") {
		m_in_sup = false;
		m_size += 2;
	}
	// no more recognized tags
}

void Layout::word(std::string_view word, SkFont& font) {
	// The algorithm is to split a word into its separable parts and to try and render all of the parts at once.
	// If this fails we remove the last separable part and try again.
	//   We keep track of these parts to be rendered later (on a new line).
	// If we have no separable parts then we need to flush and start over.
	std::vector<std::string_view> soft_hyphen_split = split(word, SOFT_HYPHEN);
	std::vector<std::string_view> hyphens_to_render_later;

	size_t failthrough;
	size_t const MAX_FAILTHROUGH = 1000;
	for (
		failthrough = 0;
		!soft_hyphen_split.empty() && failthrough < MAX_FAILTHROUGH;
		failthrough++
	) {
		std::string word_to_render;
		for (auto w : soft_hyphen_split) {
			word_to_render += w;
		}
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
				.string = std::move(word_to_render),
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
			std::string_view v = *soft_hyphen_split.erase(soft_hyphen_split.end() - 1);
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

	std::vector<SkFontMetrics> metrics(m_line.size());
	std::transform(m_line.begin(), m_line.end(), metrics.begin(), [](auto const& pos) {
		SkFontMetrics m;
		pos.font.getMetrics(&m);
		return m;
	});

	auto ascents = std::views::transform(metrics, &SkFontMetrics::fAscent);
	// ascent in skia is typically a negative number, but for Tk it's positive...
	float max_ascent = -std::ranges::min(ascents);

	// todo: metrics.fLeading
	float baseline = m_cursor_y + max_ascent * 1.25;

	// Skia draws text from the baseline, not from the NW like Tkinter
	for (auto & pos : m_line) {
		if (pos.is_super_text) {
			pos.y = baseline - max_ascent / 2;
		} else {
			pos.y = baseline;
		}
	}

	if (m_in_title || m_right_align) {
		auto const& w = m_line.back();
		float right_side_gap = (float) m_width - w.x - w.width - (float) HSTEP;

		float change = (m_in_title) ? right_side_gap / 2 : right_side_gap;
		for (auto &pos : m_line) {
			pos.x += change;
		}
	}

	auto descents = std::views::transform(metrics, &SkFontMetrics::fDescent);
	float max_descent = std::ranges::max(descents);

	// todo: metrics.fLeading
	m_cursor_y = baseline + max_descent * 1.25;
	m_cursor_x = HSTEP;
	m_display_list.insert(m_display_list.end(), m_line.begin(), m_line.end());
	m_line = {};
	assert(m_cursor_y + VSTEP >= m_must_render_up_to_y);
	m_must_render_up_to_y = m_cursor_y + VSTEP;
}
