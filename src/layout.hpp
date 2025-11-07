#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include "parsers.hpp"
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
	std::shared_ptr<SkFont> font;
	SkColor color;

	static std::shared_ptr<DrawText> create(float left, float top, float width, std::string text, std::shared_ptr<SkFont> font, SkColor color);

	void execute(float scroll, SkCanvas& canvas) override;

	DrawText(float left, float top, float right, float bottom, std::string text, std::shared_ptr<SkFont> font, SkColor color);
};

struct DrawRect : public DrawCommand {
	SkColor color;

	static std::shared_ptr<DrawRect> createLTRB(float left, float top, float right, float bottom, SkColor color);

	void execute(float scroll, SkCanvas& canvas) override;

	DrawRect(float left, float top, float right, float bottom, SkColor color);
};

struct DrawOutline : public DrawCommand {
	SkColor color;
	float thickness;

	static std::shared_ptr<DrawOutline> create(SkRect rect, SkColor color, float thickness);

	void execute(float scroll, SkCanvas& canvas) override;

	DrawOutline(SkRect rect, SkColor color, float thickness);
};

struct DrawLine : public DrawCommand {
	SkColor color;
	float thickness;

	static std::shared_ptr<DrawLine> create(float x1, float y1, float x2, float y2, SkColor color, float thickness);

	void execute(float scroll, SkCanvas& canvas) override;

	DrawLine(float x1, float y1, float x2, float y2, SkColor color, float thickness);
};

// todo: nest inside of FontCache (std::hash is giving me some trouble)
struct FontInfo {
	// todo: find a way to remove this super bloat!
	std::string name;
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
	std::unordered_map<FontInfo, std::shared_ptr<SkFont>> m_fonts;
	std::unordered_map<std::string, FontType> m_font_types;

public:
	FontCache();
	void add_type(std::string name, FontType type);

	std::shared_ptr<SkFont> get_font(std::string const& name, size_t size, bool bold, bool italic);
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
// pass key idiom seems to be the only way? kinda gross!
public:
	float m_x = -1;
	float m_y = -1;
	float m_width = -1;
	float m_height = -1;

	std::vector<std::shared_ptr<Node>> m_nodes;
	std::weak_ptr<LayoutBase> m_parent;
	std::vector<std::shared_ptr<LayoutBase>> m_children;

	LayoutBase(std::vector<std::shared_ptr<Node>> nodes, std::weak_ptr<LayoutBase> parent);
	void print_layout(int indent = 0);
	virtual void layout(FontCache& font_cache) = 0;
	virtual void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const = 0;
	virtual ~LayoutBase() = default;
};

struct TextLayout : public LayoutBase {
	std::weak_ptr<LayoutBase> m_previous;
	std::string m_word;
	std::shared_ptr<SkFont> m_font;

	TextLayout(std::vector<std::shared_ptr<Node>> nodes, std::string word, std::shared_ptr<LayoutBase> parent, std::weak_ptr<LayoutBase> previous);
	void layout(FontCache& font_cache) override;
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;
};

class LineLayout : public LayoutBase {
	std::weak_ptr<LayoutBase> m_previous;

public:
	LineLayout(std::vector<std::shared_ptr<Node>> nodes, std::shared_ptr<LayoutBase> parent, std::weak_ptr<LayoutBase> previous);
	void layout(FontCache& font_cache) override;
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;
};

class BlockLayout : public LayoutBase {
	float m_cursor_x = 0;
	bool m_in_sup = false;

	std::weak_ptr<BlockLayout> m_previous;

public:
	BlockLayout(std::vector<std::shared_ptr<Node>> node, std::shared_ptr<LayoutBase> parent, std::weak_ptr<BlockLayout> previous);
	void layout(FontCache& font_cache) override;
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;

private:
	void recurse(std::shared_ptr<Node> node, FontCache& font_cache);
	void word(std::string_view word, std::shared_ptr<Node> node, SkFont& font);
	void new_line();
	LayoutMode layout_mode() const;
};

class DocumentLayout : public LayoutBase {
	float m_screen_width;
public:
	DocumentLayout(std::shared_ptr<Node> node, float screen_width);
	void layout(FontCache& font_cache) override;
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;
};

void paint_tree(LayoutBase const& layout, std::vector<std::shared_ptr<DrawCommand>>& display_list);
