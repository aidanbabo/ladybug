#include "include/core/SkColor.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkFontMetrics.h"

#include "utils.hpp"
#include "layout.hpp"

#include <cassert>

#include <charconv>
#include <iostream>
#include <memory>
#include <numeric>
#include <ranges>

constexpr float INPUT_WIDTH_PX = 200;
constexpr std::array TEXT_LIKE_ELEMENTS = { "a", "i", "b", "strong", "em", "small", "sub", "sup", "ins", "del", "mark" };
constexpr float LI_BULLET_SPACING = 3 * HSTEP;

static std::optional<SkColor> parse_hex_color(std::string_view hex) {
	bool is_short = hex.size() == 3 || hex.size() == 4;
	bool is_long = hex.size() == 6 || hex.size() == 8;
	if (!(is_short || is_long)) {
		return std::nullopt;
	}
	bool has_alpha = hex.size() == 4 || hex.size() == 8;
	size_t step = is_short ? 1 : 2;

	uint8_t red, green, blue, alpha = is_short ? 0x0f : 0xff;

	if (std::from_chars(hex.data(), hex.data() + step, red, 16).ec != std::errc{}) {
		return std::nullopt;
	}
	if (std::from_chars(hex.data() + step, hex.data() + 2 * step, green, 16).ec != std::errc{}) {
		return std::nullopt;
	}
	if (std::from_chars(hex.data() + 2 * step, hex.data() + 3 * step, blue, 16).ec != std::errc{}) {
		return std::nullopt;
	}
	if (has_alpha && std::from_chars(hex.data() + 3 * step, hex.data() + 4 * step, alpha, 16).ec != std::errc{}) {
		return std::nullopt;
	}
	if (is_short) {
		red |= red << 1;
		green |= green << 1;
		blue |= blue << 1;
		alpha |= alpha << 1;
	}
	return SkColorSetARGB(alpha, red, green, blue);
}

static std::optional<SkColor> parse_color(std::string_view color) {
	static const std::unordered_map<std::string_view, SkColor> COLOR_NAMES = {
		{ "white", SK_ColorWHITE },
		{ "black", SK_ColorBLACK },
		{ "red", SK_ColorRED },
		{ "blue", SK_ColorBLUE },
		{ "gray", SK_ColorGRAY },
		{ "grey", SK_ColorGRAY },
		{ "lightgray", SK_ColorLTGRAY },
		{ "lightgrey", SK_ColorLTGRAY },
		{ "lightblue", SkColorSetRGB(0xad, 0xd8, 0xe6) },
		{ "yellow", SK_ColorYELLOW },
		{ "magenta", SK_ColorMAGENTA },
		// todo: not orange lol
		{ "orange", SkColorSetRGB(0xff, 0xff, 0x00) },
	};

	if (auto c = COLOR_NAMES.find(color); c != COLOR_NAMES.end()) {
		return c->second;
	}
	if (color.starts_with("#")) {
		if (auto c = parse_hex_color(color.substr(1))) {
			return c;
		}
	}

	std::cerr << "Cannot parse color: '" << color << "'" << std::endl;
	return std::nullopt;
}

static std::optional<float> parse_pixels(std::string_view s) {
	if (!s.ends_with("px")) {
		return std::nullopt;
	}
	float pixels;
	if (std::from_chars(s.data(), s.data() + s.size() - 2, pixels).ec != std::errc{}) {
		return std::nullopt;
	}
	return pixels;
}

static FontInfo font_info_from_node(Node const& node) {
	bool is_bold = false;
	if (node.styles.find("font-weight")->second == "bold") {
		is_bold = true;
	}

	bool is_italic = false;
	if (node.styles.find("font-style")->second == "italic") {
		is_italic = true;
	}

	std::string_view font_family { node.styles.find("font-family")->second };

	std::string_view size_str { node.styles.find("font-size")->second };
	size_t size = 16;
	if (auto s = parse_pixels(size_str)) {
		size = *s;
	}
	// why do we do this? map between px and Tk screen space?
	size = static_cast<float>(size) * 0.75;

	return FontInfo {
		.name = font_family,
		.size = size,
		.bold = is_bold,
		.italic = is_italic,
	};
}

LayoutBase::LayoutBase(std::weak_ptr<LayoutBase> parent)
	: m_parent(parent)
{}

void LayoutBase::print_layout(int indent) const {
	for (int i = 0; i < indent; i++) {
		std::cout << " ";
	}
	std::cout << "{x:" << m_x << ",y:" << m_y << ",w:" << m_width << ",h:" << m_height << "} ";

	this->print();
	std::cout << std::endl;

	for (auto const& child : m_children) {
		child->print_layout(indent + 2);
	}
}

void InputLayout::print() const {
	std::cout << "Input: ";
}

void TextLayout::print() const {
	std::cout << "Text: " << m_word;
}

void LineLayout::print() const {
	std::cout << "Line: ";
}

void BlockLayout::print() const {
	for (auto node : m_nodes) {
		if (node->type == NodeType::Text) {
			std::cout << "<no element> ";
		} else {
			auto element = static_cast<Element const&>(*node);
			std::cout << "<" << element.tag << "> ";
		}
	}
}

void DocumentLayout::print() const {
	std::cout << "Document: ";
}

SkRect LayoutBase::self_rect() const {
	return SkRect::MakeLTRB(m_x, m_y, m_x + m_width, m_y + m_height);
}

bool LayoutBase::should_paint() const {
	return true;
}

InputLayout::InputLayout(std::shared_ptr<Node> node, std::shared_ptr<LayoutBase> parent, std::weak_ptr<LayoutBase> previous)
	: LayoutBase(parent)
	, m_node(std::move(node))
	, m_previous(previous)
{}

void InputLayout::layout(FontCache& font_cache) {
	FontInfo info = font_info_from_node(*m_node);
	m_font = font_cache.get_font(info);

	m_width = INPUT_WIDTH_PX;
	if (auto prev = m_previous.lock()) {
		m_x = prev->m_x + prev->m_width;
		// todo: sort of hacky
		if (auto text_prev = dynamic_cast<TextLayout *>(&*prev)) {
			auto space = text_prev->m_font->measureText(" ", 1, SkTextEncoding::kUTF8);
			m_x += space;
		} else if (auto input_prev = dynamic_cast<TextLayout *>(&*prev)) {
			auto space = input_prev->m_font->measureText(" ", 1, SkTextEncoding::kUTF8);
			m_x += space;
		} else {
			auto space = m_font->measureText(" ", 1, SkTextEncoding::kUTF8);
			m_x += space;
		}
	} else {
		m_x = m_parent.lock()->m_x;
	}

	SkFontMetrics m;
	m_font->getMetrics(&m);
	m_height = m.fDescent - m.fAscent; // todo: should be linespace

}

void InputLayout::paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const {
	auto bgcolor_iter = m_node->styles.find("background-color");
	std::string_view bgcolor { (bgcolor_iter != m_node->styles.end()) ? bgcolor_iter->second : "transparent" };
	if (bgcolor != "transparent") {
		auto color = parse_color(bgcolor);
		if (color) {
			auto rect = DrawRect::create(self_rect(), *color);
			commands.push_back(rect);
		}
	}

	if (m_node->type == NodeType::Text) {
		return;
	}
	auto element = static_cast<Element const&>(*m_node);

	std::string text;
	if (element.tag == "input") {
		if (auto v = element.attributes.find("value"); v != element.attributes.end()) {
			text = v->second;
		}
	} else if (element.tag == "button") {
		if (element.children.size() == 1 && element.children[0]->type == NodeType::Text) {
			text = static_cast<Text const&>(*element.children[0]).text;
		} else {
			std::cerr << "Ignoring HTML contents inside button" << std::endl;
		}
	}

	if (m_node->is_focused) {
		auto cx = m_x + m_font->measureText(text.data(), text.size(), SkTextEncoding::kUTF8);
		commands.push_back(DrawLine::create(cx, m_y, cx, m_y + m_height, SK_ColorBLACK, 1));
	}

	auto color_str { m_node->styles["color"] };
	auto color { parse_color(color_str).value_or(SK_ColorBLACK) };
	auto cmd { DrawText::create(m_x, m_y, m_width, m_height, text, m_font, color) };
	commands.push_back(cmd);
}

std::vector<std::shared_ptr<Node>> InputLayout::nodes() const {
	return std::vector{m_node};
}

TextLayout::TextLayout(std::shared_ptr<Node> node, std::string word, std::shared_ptr<LayoutBase> parent, std::weak_ptr<LayoutBase> previous)
	: LayoutBase(parent)
	, m_node(std::move(node))
	, m_previous(previous)
	, m_word(std::move(word))
{}

void TextLayout::layout(FontCache& font_cache) {
	FontInfo info = font_info_from_node(*m_node);
	m_font = font_cache.get_font(info);

	m_width = m_font->measureText(m_word.data(), m_word.size(), SkTextEncoding::kUTF8);
	if (auto prev = m_previous.lock()) {
		m_x = prev->m_x + prev->m_width;
		// todo: sort of hacky
		if (auto text_prev = dynamic_cast<TextLayout *>(&*prev)) {
			auto space = text_prev->m_font->measureText(" ", 1, SkTextEncoding::kUTF8);
			m_x += space;
		} else if (auto input_prev = dynamic_cast<TextLayout *>(&*prev)) {
			auto space = input_prev->m_font->measureText(" ", 1, SkTextEncoding::kUTF8);
			m_x += space;
		} else {
			auto space = m_font->measureText(" ", 1, SkTextEncoding::kUTF8);
			m_x += space;
		}
	} else {
		m_x = m_parent.lock()->m_x;
	}

	SkFontMetrics m;
	m_font->getMetrics(&m);
	m_height = m.fDescent - m.fAscent; // todo: should be linespace

}

void TextLayout::paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const {
	auto color_str { m_node->styles["color"] };
	auto color { parse_color(color_str).value_or(SK_ColorBLACK) };
	auto cmd { DrawText::create(m_x, m_y, m_width, m_height, m_word, m_font, color) };
	commands.push_back(cmd);
}

std::vector<std::shared_ptr<Node>> TextLayout::nodes() const {
	return std::vector{m_node};
}

LineLayout::LineLayout(std::shared_ptr<Node> node, std::shared_ptr<LayoutBase> parent, std::weak_ptr<LayoutBase> previous)
	: LayoutBase(parent)
	, m_node(node)
	, m_previous(previous)
{}

void LineLayout::layout(FontCache& font_cache) {
	m_width = m_parent.lock()->m_width;
	m_x = m_parent.lock()->m_x;
	if (auto prev = m_previous.lock()) {
		m_y = prev->m_y + prev->m_height;
	} else {
		m_y = m_parent.lock()->m_y;
	}
	if (m_children.empty()) {
		m_height = 0;
		if (auto prev = m_previous.lock()) {
			m_height = prev->m_height;
		}
		return;
	}

	for (auto word : m_children) {
		word->layout(font_cache);
	}

	std::vector<SkFontMetrics> metrics(m_children.size());
	std::transform(m_children.begin(), m_children.end(), metrics.begin(), [](auto const& child) {
		auto f = [&]() -> std::shared_ptr<SkFont> {
			auto text = dynamic_cast<TextLayout const*>(&*child);
			auto input = dynamic_cast<InputLayout const*>(&*child);
			if (text) {
				return text->m_font;
			} else if (input) {
				return input->m_font;
			} else {
				assert(false && "Children should only be text or input nodes");
			}
		}();
		SkFontMetrics m;
		f->getMetrics(&m);
		return m;
	});

	auto ascents = std::views::transform(metrics, &SkFontMetrics::fAscent);
	// ascent in skia is typically a negative number, but for Tk it's positive...
	float max_ascent = -std::ranges::min(ascents);
	float baseline = m_y + max_ascent * 1.25;
	for (size_t i = 0; i < m_children.size(); i++) {
		// todo: Supertext. We could put a flag on each text-layout?
		m_children[i]->m_y = baseline + metrics[i].fAscent;
	}
	auto descents = std::views::transform(metrics, &SkFontMetrics::fDescent);
	float max_descent = std::ranges::max(descents);
	m_height = 1.25 * (max_ascent + max_descent);
}

void LineLayout::paint(std::vector<std::shared_ptr<DrawCommand>>&) const
{}

std::vector<std::shared_ptr<Node>> LineLayout::nodes() const {
	return std::vector{m_node};
}

BlockLayout::BlockLayout(std::vector<std::shared_ptr<Node>> nodes, std::shared_ptr<LayoutBase> parent, std::weak_ptr<BlockLayout> previous)
	: LayoutBase(parent)
	, m_nodes(nodes)
	, m_previous(previous)
{
	assert(!m_parent.expired());
	if (m_nodes.size() > 1) {
		for (auto const& node : m_nodes) {
			if (node->type != NodeType::Element) {
				continue;
			}
			auto element = static_cast<Element const&>(*node);
			assert(std::find(TEXT_LIKE_ELEMENTS.begin(), TEXT_LIKE_ELEMENTS.end(), element.tag) != TEXT_LIKE_ELEMENTS.end() && "if there are multiple element children they must all be text like");
		}
	}
}

LayoutMode BlockLayout::layout_mode() const {
	if (std::ranges::all_of(m_nodes, [](std::shared_ptr<Node> n) { return n->type == NodeType::Text; })) {
		return LayoutMode::Inline;
	}
	for (auto const& node : m_nodes) {
		for (auto const& child : node->children) {
			if (child->type == NodeType::Element) {
				auto element = static_cast<Element const&>(*child);
				if (auto display = element.styles.find("display"); display != element.attributes.end() && display->second == "block") {
					return LayoutMode::Block;
				}
			}
		}
	}
	if (std::ranges::all_of(m_nodes, [](std::shared_ptr<Node> n) { return !n->children.empty(); })) {
		return LayoutMode::Inline;
	}
	return LayoutMode::Block;
}

bool BlockLayout::should_paint() const {
	return std::ranges::any_of(m_nodes, [](auto n) {
		if (n->type == NodeType::Text) {
			return true;
		}
		auto el = static_cast<Element const&>(*n);
		return el.tag != "input" && el.tag != "button";
	});
}

// todo: BlockLayout::layout can have multiple nodes
// stop using m_nodes[0]
void BlockLayout::layout(FontCache& font_cache) {
	m_x = m_parent.lock()->m_x;
	{
		std::optional<float> width_opt = std::nullopt;
		if (auto width = m_nodes[0]->styles.find("width"); width != m_nodes[0]->styles.end() && width->second != "auto") {
			width_opt = parse_pixels(width->second);
		}
		if (width_opt) {
			m_width = *width_opt;
		} else {
			m_width = m_parent.lock()->m_width;
		}
	}
	if (m_nodes[0]->type == NodeType::Element) {
		auto element = static_cast<Element const&>(*m_nodes[0]);
		if (element.tag == "li") {
			assert(m_nodes.size() == 1 && "layout isn't shared between li");
			m_x += LI_BULLET_SPACING;
			m_width -= LI_BULLET_SPACING;
		}
		if (element.tag == "script" || element.tag == "style") {
			m_height = 0;
			return;
		}
	}
	if (auto previous = m_previous.lock()) {
		m_y = previous->m_y + previous->m_height; 
	} else {
		// can't access protected member :(
		m_y = m_parent.lock()->m_y;
	}

	LayoutMode mode = layout_mode();
	if (mode == LayoutMode::Block) {
		std::shared_ptr<LayoutBase> shared = shared_from_this();
		std::weak_ptr<BlockLayout> previous;
		std::vector<std::shared_ptr<Node>> nodes;
		for (auto const& node : m_nodes) {
			for (auto const& child : node->children) {
				if (child->type == NodeType::Element && static_cast<Element const&>(*child).tag == "head") {
					continue;
				}

				switch (child->type) {
				case NodeType::Text: {
					nodes.push_back(child);
					break;
				}
				case NodeType::Element: {
					auto element = static_cast<Element const&>(*child);
					if (std::find(TEXT_LIKE_ELEMENTS.begin(), TEXT_LIKE_ELEMENTS.end(), element.tag) == TEXT_LIKE_ELEMENTS.end()) {
						if (!nodes.empty()) {
							auto next = std::make_shared<BlockLayout>(nodes, shared, previous);
							m_children.push_back(next);
							previous = next;
							nodes.clear();
						}
						auto next = std::make_shared<BlockLayout>(std::vector{ child }, shared, previous);
						m_children.push_back(next);
						previous = next;
					} else {
						nodes.push_back(child);
					}
					break;
				}
				default: {
					assert(false && "unreachable");
				}
				}
			}
		}
		if (!nodes.empty()) {
			auto next = std::make_shared<BlockLayout>(nodes, shared, previous);
			m_children.push_back(next);
		}
	} else if (mode == LayoutMode::Inline) {
		new_line(m_nodes[0]);
		for (auto node : m_nodes) {
			recurse(node, font_cache);
		}
	} else {
		assert(false && "unreachable");
	}

	for (auto child : m_children) {
		child->layout(font_cache);
	}

	std::optional<float> height_opt = std::nullopt;
	if (auto height = m_nodes[0]->styles.find("height"); height != m_nodes[0]->styles.end() && height->second != "auto") {
		height_opt = parse_pixels(height->second);
	}
	if (height_opt) {
		m_height = *height_opt;
	} else {
		m_height = std::transform_reduce(m_children.begin(), m_children.end(), 0.0, std::plus<>{}, [](auto const& c) { return c->m_height; });
	}
}

void BlockLayout::paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const {
	for (auto const& node : m_nodes) {
		if (node->type == NodeType::Element) {
			auto element = static_cast<Element const&>(*node);
			auto bgcolor_iter = element.styles.find("background-color");
			std::string bgcolor = (bgcolor_iter != element.styles.end()) ? bgcolor_iter->second : std::string{ "transparent" };
			if (bgcolor != "transparent") {
				auto color = parse_color(bgcolor);
				if (color) {
					auto rect = DrawRect::create(self_rect(), *color);
					commands.push_back(rect);
				}
			}

			if (element.tag == "li") {
				constexpr float LI_BULLET_SIZE = ((float)VSTEP) / 3.0;
				constexpr float VERTICAL_SPACING = VSTEP - LI_BULLET_SIZE;
				constexpr float HORIZONTAL_SPACING = LI_BULLET_SPACING - LI_BULLET_SIZE;
				std::shared_ptr<DrawCommand> rect = DrawRect::create(SkRect::MakeLTRB(
					m_x - 0.5 * HORIZONTAL_SPACING - LI_BULLET_SIZE,
					m_y + 0.5 * VERTICAL_SPACING,
					m_x - 0.5 * HORIZONTAL_SPACING,
					m_y + 0.5 * VERTICAL_SPACING + LI_BULLET_SIZE),
					SK_ColorBLACK);
				commands.push_back(rect);
			}
		}
	}
}

void BlockLayout::recurse(std::shared_ptr<Node> node, FontCache& font_cache) {
	if (node->type == NodeType::Text) {

		FontInfo info = font_info_from_node(*node);
		auto font = font_cache.get_font(info);

		Text const& text = static_cast<Text const&>(*node);
		std::vector<std::string_view> words = split_on_any(text.text);
		for (auto w : words) {
			w = trim_whitespace(w);
			if (w != "") {
				word(w, node, *font);
			}
		}
	} else if (node->type == NodeType::Element) {
		Element const& element = static_cast<Element const&>(*node);
		if (element.tag == "br") {
			new_line(node);
		} else if (element.tag == "input" || element.tag == "button") {
			input(node, font_cache);
		} else if (element.tag == "script" || element.tag == "style") {
			// do nothing
		} else {
			for (auto child : element.children) {
				recurse(child, font_cache);
			}
		}
		if (element.tag == "p") {
			// todo: move to css? margin bottom?
			new_line(node);
			//m_cursor_y += VSTEP;
		}
	} else {
		assert(false && "unreachable");
	}
}

void BlockLayout::input(std::shared_ptr<Node> node, FontCache& font_cache) {
	auto w = INPUT_WIDTH_PX;
	if (m_cursor_x + w > m_width) {
		new_line(node);
	}
	assert(!m_children.empty());
	auto line = m_children.back();
	auto previous_word = !line->m_children.empty() ? line->m_children.back() : nullptr;
	auto input = std::make_shared<InputLayout>(node, line, previous_word);
	line->m_children.push_back(input);

	FontInfo info = font_info_from_node(*node);
	auto font = font_cache.get_font(info);
	m_cursor_x += w + font->measureText(" ", 1, SkTextEncoding::kUTF8);
}

void BlockLayout::word(std::string_view word, std::shared_ptr<Node> node, SkFont& font) {
	// todo: discard styling if the color fails to parse
	// this will need to be in the cascade i think

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
		auto text_width_max = m_width - m_cursor_x;
		auto w = font.measureText(word_to_render.c_str(), word_to_render.size(), SkTextEncoding::kUTF8);

		if (w < text_width_max || (m_cursor_x == 0 && soft_hyphen_split.size() == 1)) {
			// If the word fits or will never fit at the current resolution we render it!
			assert(!m_children.empty());
			auto line = m_children.back();
			auto previous_word = line->m_children.empty() ? nullptr : line->m_children.back();
			auto text = std::make_shared<TextLayout>(node, std::move(word_to_render), line, previous_word);
			line->m_children.push_back(text);
			//m_line.push_back(pos);

			m_cursor_x += w + font.measureText(" ", 1, SkTextEncoding::kUTF8);

			// Optimization: If there is more work, it is because this line is full, so we must flush anyway!
			// This prevents us from trying to split the remaining string over and over when none of it will fit.
			if (!hyphens_to_render_later.empty()) {
				new_line(node);
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
			new_line(node);
			soft_hyphen_split.insert(soft_hyphen_split.end(), hyphens_to_render_later.begin(), hyphens_to_render_later.end());
			hyphens_to_render_later = {};
		}
	}

	if (failthrough == MAX_FAILTHROUGH) {
		std::cerr << "Failed to render text: " << word << std::endl;
	}
}

void BlockLayout::new_line(std::shared_ptr<Node> node) {
	m_cursor_x = 0;
	auto last_line = m_children.empty() ? nullptr : m_children.back();
	auto new_line = std::make_shared<LineLayout>(node, shared_from_this(), last_line);
	m_children.push_back(new_line);
}

	// todo: reintroduce support for centering and right aligning text.
	// The problem is sometimes we call flush not inside `word` and so we don't have an obvious `Node` instance to read of styles from.
	/*
	bool right_align = false;
	if (auto ta = containing_parent.styles.find("text-align"); ta != containing_parent.styles.end() && (ta->second == "right" || ta->second == "end")) {
		right_align = true;
	}
	bool center_align = false;
	if (auto ta = containing_parent.styles.find("text-align"); ta != containing_parent.styles.end() && ta->second == "center") {
		center_align = true;
	}
	if (center_align || right_align) {
		auto const& w = m_line.back();
		float right_side_gap = (float) m_width - w.left - w.width - (float) HSTEP;

		float change = (center_align) ? right_side_gap / 2 : right_side_gap;
		for (auto &pos : m_line) {
			pos.left += change;
		}
	}
	*/

std::vector<std::shared_ptr<Node>> BlockLayout::nodes() const {
	return m_nodes;
}

DocumentLayout::DocumentLayout(std::shared_ptr<Node> node, float screen_width)
	: LayoutBase(std::weak_ptr<LayoutBase>())
	, m_node(node)
	, m_screen_width(screen_width)
{}

void DocumentLayout::layout(FontCache& font_cache) {
	std::shared_ptr<LayoutBase> shared = shared_from_this();
	auto child = std::make_shared<BlockLayout>(std::vector{m_node}, shared, std::weak_ptr<BlockLayout>());
	m_children.push_back(child);
	m_x = HSTEP;
	m_y = VSTEP;
	m_width = m_screen_width - 2 * HSTEP;
	child->layout(font_cache);
	m_height = child->m_height;
}

void DocumentLayout::paint(std::vector<std::shared_ptr<DrawCommand>>&) const {}

std::vector<std::shared_ptr<Node>> DocumentLayout::nodes() const {
	return std::vector{m_node};
}

void paint_tree(LayoutBase const& layout, std::vector<std::shared_ptr<DrawCommand>>& commands) {
	if (layout.should_paint()) {
		layout.paint(commands);
	}

	for (auto child : layout.m_children) {
		paint_tree(*child, commands);
	}
}

void layout_tree_to_list(std::shared_ptr<LayoutBase> node, std::vector<std::shared_ptr<LayoutBase>>& list) {
	list.push_back(node);
	for (auto const& c : node->m_children) {
		layout_tree_to_list(c, list);
	}
}
