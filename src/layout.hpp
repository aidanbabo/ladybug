#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include "utils.hpp"
#include "include/core/SkFont.h"

enum class TokenTag {
	Text,
	Tag,
};

struct Token {
	TokenTag tag;
	std::string data;
};

std::vector<Token> lex(std::string_view body);

struct StringPosition {
	float x, y;
	float width;
	// todo: support unicode
	std::string string;
	// todo: is copying this around cheap?
	SkFont font;
	bool is_super_text;
};

struct FontInfo {
// todo: fields are module-private
	size_t size;
	bool bold;
	bool italic;

	bool operator==(const FontInfo& other) const noexcept;
};


template<>
struct std::hash<FontInfo> {
	size_t operator()(const FontInfo& f) const noexcept;
};

struct FontType {
	sk_sp<SkTypeface> normal;
	sk_sp<SkTypeface> bold;
	sk_sp<SkTypeface> italic;
	sk_sp<SkTypeface> bold_italic;
};

class FontCache {
	std::unordered_map<FontInfo, SkFont> m_fonts;
	FontType m_font_type;

public:
	FontCache(FontType ty);

	SkFont& get_font(size_t size, bool bold, bool italic);
};

struct ComputedLayout {
	std::vector<StringPosition> display_list;
	float must_render_up_to_y;
};

// this approach is odd...
class Layout {
	std::vector<StringPosition> m_display_list;
	float m_cursor_x = HSTEP;
	float m_cursor_y = VSTEP;
	float m_must_render_up_to_y = VSTEP;
	bool m_is_bold = false;
	bool m_is_italic = false;
	bool m_in_title = false;
	bool m_in_sup = false;
	int m_size = 12;
	// positions will have useless y coordinates
	std::vector<StringPosition> m_line;

	int m_width;
	bool m_right_align;

public:
	Layout(std::vector<Token> const& tokens, FontCache& font_cache, int width, bool right_align);
	ComputedLayout computed() const;

private:
	void token(Token const& tok, FontCache& font_cache);
	void word(std::string_view word, SkFont& font);
	void flush();
};
