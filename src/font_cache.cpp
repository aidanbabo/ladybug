#include "font_cache.hpp"
#include "utils.hpp"

#include <cassert>
#include <iostream>

bool FontInfo::operator==(const FontInfo& other) const noexcept {
	return size == other.size && bold == other.bold && italic == other.italic && name == other.name;
}

size_t std::hash<FontInfo>::operator()(const FontInfo& f) const noexcept {
	size_t seed = 0;
	combine_hash(seed, std::hash<std::string_view>{}(f.name));
	combine_hash(seed, std::hash<size_t>{}(f.size));
	combine_hash(seed, std::hash<bool>{}(f.bold));
	combine_hash(seed, std::hash<bool>{}(f.italic));
	return seed;
}

FontCache::FontCache() : m_fonts() {}

void FontCache::add_type(std::string name, FontType type) {
	m_font_types.insert({name, type});
}

std::shared_ptr<SkFont> FontCache::get_font(std::string_view name, size_t size, bool bold, bool italic) {
	auto info = FontInfo {
		.name = name,
		.size = size,
		.bold = bold,
		.italic = italic,
	};
	if (auto f = m_fonts.find(info); f != m_fonts.end()) {
		return f->second;
	}

	auto font_type = m_font_types.find(name);
	if (font_type == m_font_types.end()) {
		std::cerr << "Unsupported font " << name << std::endl;
		font_type = m_font_types.find("times");
		assert(font_type != m_font_types.end());
	}
	sk_sp<SkTypeface> typeface = [&] {
		if (!bold && !italic) {
			return font_type->second.normal;
		} else if (bold && !italic) {
			return font_type->second.bold;
		} else if (!bold && italic) {
			return font_type->second.italic;
		} else if (bold && italic) {
			return font_type->second.bold_italic;
		} else {
			assert(false && "unreachable");
		}
	}();
	m_fonts[info] = std::make_shared<SkFont>(typeface, size);
	return m_fonts[info];
}
