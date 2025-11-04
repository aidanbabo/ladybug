#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include "utils.hpp"
#include "html_parser.hpp"
#include "include/core/SkFont.h"
#include "include/core/SkCanvas.h"

// todo: move to it's own hpp-cpp pair
struct DrawCommand {
	float left;
	float top;
	float right;
	float bottom;

	virtual void execute(float scroll, SkCanvas& canvas) = 0;
	virtual ~DrawCommand() = default;
	DrawCommand(float left, float top, float right, float bottom);
};

struct DrawText : public DrawCommand {
	// todo: support unicode
	std::string text;
	// todo: is copying this around cheap?
	SkFont font;
	// todo: find a way to remove, we only use this during layout
	bool is_super_text;

	static std::shared_ptr<DrawText> create(float left, float top, float width, std::string text, SkFont font, bool is_super_text = false);

	void execute(float scroll, SkCanvas& canvas);

	DrawText(float left, float top, float right, float bottom, std::string text, SkFont font, bool is_super_text);
};

struct DrawRect : public DrawCommand {
	SkColor color;

	static std::shared_ptr<DrawRect> createLTRB(float left, float top, float right, float bottom, SkColor color);

	void execute(float scroll, SkCanvas& canvas);

	DrawRect(float left, float top, float right, float bottom, SkColor color);
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

enum class LayoutMode {
	Inline,
	Block,
};

class BlockLayout;

// hack to have a shared_ptr as a receiver (almost)
// todo: no more constructors, only factories that use shared
class LayoutBase : public std::enable_shared_from_this<LayoutBase> {
// todo: make protected, why doesn't it work?
public:
	float m_x = -1;
	float m_y = -1;
	float m_width = -1;
	float m_height = -1;

	std::vector<std::shared_ptr<Node>> m_nodes;
	std::weak_ptr<LayoutBase> m_parent;
	std::vector<std::shared_ptr<BlockLayout>> m_children;

	LayoutBase(std::vector<std::shared_ptr<Node>> nodes, std::weak_ptr<LayoutBase> parent);
	void print_layout(int indent = 0);
	virtual void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const = 0;
	virtual ~LayoutBase() = default;
};

struct StringPosition {
	float left;
	// filled in later
	float top = -1;
	float width;
	std::string text;
	SkFont font;
	bool is_super_text;
};

class BlockLayout : public LayoutBase {
	std::vector<StringPosition> m_display_list;
	float m_cursor_x = 0;
	float m_cursor_y = 0;
	// todo: adjust to 0?
	float m_must_render_up_to_y = VSTEP;
	bool m_is_bold = false;
	bool m_is_italic = false;
	bool m_in_title = false;
	bool m_in_sup = false;
	int m_size = 12;
	std::vector<StringPosition> m_line;

	std::weak_ptr<BlockLayout> m_previous;

public:
	BlockLayout(std::vector<std::shared_ptr<Node>> node, std::shared_ptr<LayoutBase> parent, std::weak_ptr<BlockLayout> previous);
	void layout(FontCache& font_cache, bool right_align);
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;

private:
	void recurse(Node const& tok, FontCache& font_cache, bool right_align);
	void open_tag(Element const& tag, bool right_align);
	void close_tag(Element const& tag, bool right_align);
	void word(std::string_view word, SkFont& font, bool right_align);
	void flush(bool right_align);
	LayoutMode layout_mode() const;
};

// todo: we need a better way to do this than inheritance
class DocumentLayout : public LayoutBase {
	// todo: can we just pass these in as well?
	FontCache& m_font_cache;
	int m_screen_width;
	bool m_right_align;

public:
	DocumentLayout(std::shared_ptr<Node> node, FontCache& font_cache, int screen_width, bool right_align);
	void layout();
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;
};

void paint_tree(LayoutBase const& layout, std::vector<std::shared_ptr<DrawCommand>>& display_list);
