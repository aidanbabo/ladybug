#pragma once
#include "include/core/SkFont.h"
#include <unordered_map>

// todo: nest inside of FontCache (std::hash is giving me some trouble)
struct FontInfo {
	std::string_view name;
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
	// So that we can index with string_view!
	struct string_hash {
		using is_transparent = void;
		[[nodiscard]] size_t operator()(const char *txt) const {
			return std::hash<std::string_view>{}(txt);
		}
		[[nodiscard]] size_t operator()(std::string_view txt) const {
			return std::hash<std::string_view>{}(txt);
		}
		[[nodiscard]] size_t operator()(const std::string &txt) const {
			return std::hash<std::string>{}(txt);
		}
	};

	std::unordered_map<FontInfo, std::shared_ptr<SkFont>> m_fonts;
	// key is a stable pointer!
	// todo: is there a way to make sure resizing the map doesn't actually move the string allocation?
	std::unordered_map<std::string, FontType, string_hash, std::equal_to<>> m_font_types;

public:
	FontCache();
	void add_type(std::string name, FontType type);

	std::shared_ptr<SkFont> get_font(std::string_view name, size_t size, bool bold, bool italic);
};
