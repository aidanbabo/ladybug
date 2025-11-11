#pragma once
#include <string>
#include <vector>

#include "draw.hpp"
#include "parsers.hpp"
#include "font_cache.hpp"
#include "include/core/SkFont.h"
#include "include/core/SkCanvas.h"

enum class LayoutMode {
	Inline,
	Block,
};

class BlockLayout;

// hack to have a shared_ptr as a receiver (almost)
// todo: no more constructors, only factories that use shared
struct LayoutBase : public std::enable_shared_from_this<LayoutBase> {
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
