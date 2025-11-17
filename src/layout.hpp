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

	std::weak_ptr<LayoutBase> m_parent;
	std::vector<std::shared_ptr<LayoutBase>> m_children;

	SkRect self_rect() const;

	explicit LayoutBase(std::weak_ptr<LayoutBase> parent);
	virtual void layout(FontCache& font_cache) = 0;
	virtual bool should_paint() const;
	virtual void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const = 0;
	virtual std::vector<std::shared_ptr<Node>> nodes() const = 0;
	void print_layout(int indent = 0) const;
	virtual ~LayoutBase() = default;
private:
	virtual void print() const = 0;
};

struct InputLayout : public LayoutBase {
	std::shared_ptr<Node> m_node;
	std::weak_ptr<LayoutBase> m_previous;
	std::shared_ptr<SkFont> m_font;

	InputLayout(std::shared_ptr<Node> node, std::shared_ptr<LayoutBase> parent, std::weak_ptr<LayoutBase> previous);
	void layout(FontCache& font_cache) override;
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;
	void print() const override;
	std::vector<std::shared_ptr<Node>> nodes() const override;
	~InputLayout() = default;
};

struct TextLayout : public LayoutBase {
	std::shared_ptr<Node> m_node;
	std::weak_ptr<LayoutBase> m_previous;
	std::string m_word;
	std::shared_ptr<SkFont> m_font;

	TextLayout(std::shared_ptr<Node> node, std::string word, std::shared_ptr<LayoutBase> parent, std::weak_ptr<LayoutBase> previous);
	void layout(FontCache& font_cache) override;
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;
	void print() const override;
	std::vector<std::shared_ptr<Node>> nodes() const override;
	~TextLayout() = default;
};

class LineLayout : public LayoutBase {
	std::shared_ptr<Node> m_node;
	std::weak_ptr<LayoutBase> m_previous;

public:
	LineLayout(std::shared_ptr<Node> node, std::shared_ptr<LayoutBase> parent, std::weak_ptr<LayoutBase> previous);
	void layout(FontCache& font_cache) override;
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;
	void print() const override;
	std::vector<std::shared_ptr<Node>> nodes() const override;
	~LineLayout() = default;
};

class BlockLayout : public LayoutBase {
	float m_cursor_x = 0;
	bool m_in_sup = false;

	std::vector<std::shared_ptr<Node>> m_nodes;
	std::weak_ptr<BlockLayout> m_previous;

public:
	BlockLayout(std::vector<std::shared_ptr<Node>> node, std::shared_ptr<LayoutBase> parent, std::weak_ptr<BlockLayout> previous);
	void layout(FontCache& font_cache) override;
	bool should_paint() const override;
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;
	void print() const override;
	std::vector<std::shared_ptr<Node>> nodes() const override;
	~BlockLayout() = default;

private:
	void recurse(std::shared_ptr<Node> node, FontCache& font_cache);
	void word(std::string_view word, std::shared_ptr<Node> node, SkFont& font);
	void input(std::shared_ptr<Node> node, FontCache& font_cache);
	void new_line(std::shared_ptr<Node> node);
	LayoutMode layout_mode() const;
};

class DocumentLayout : public LayoutBase {
	std::shared_ptr<Node> m_node;
	float m_screen_width;
public:
	DocumentLayout(std::shared_ptr<Node> node, float screen_width);
	void layout(FontCache& font_cache) override;
	void paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const override;
	void print() const override;
	std::vector<std::shared_ptr<Node>> nodes() const override;
	~DocumentLayout() = default;
};

void paint_tree(LayoutBase const& layout, std::vector<std::shared_ptr<DrawCommand>>& display_list);
