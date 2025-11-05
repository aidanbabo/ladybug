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
		{ "blue", SK_ColorBLUE },
		{ "gray", SK_ColorGRAY },
		{ "grey", SK_ColorGRAY },
		{ "lightgray", SK_ColorLTGRAY },
		{ "lightgrey", SK_ColorLTGRAY },
		{ "lightblue", SkColorSetRGB(0xad, 0xd8, 0xe6) },
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


DrawCommand::DrawCommand(float left, float top, float right, float bottom)
	: left(left)
	, top(top)
	, right(right)
	, bottom(bottom)
{}

DrawText::DrawText(float left, float top, float right, float bottom, std::string text, SkFont font, SkColor color)
	: DrawCommand(left, top, right, bottom)
	, text(text)
	, font(font)
	, color(color)
{}

std::shared_ptr<DrawText> DrawText::create(float left, float top, float width, std::string text, SkFont font, SkColor color) {
	SkFontMetrics m;
	font.getMetrics(&m);
	// todo: leading?
	float bottom = top + m.fDescent - m.fAscent;
	return std::make_shared<DrawText>(left, top, left + width, bottom, text, font, color);
}

void DrawText::execute(float scroll, SkCanvas& canvas) {
	SkPaint paint;
	paint.setColor(color);
	canvas.drawString(text.c_str(), left, top - scroll, font, paint);
}

DrawRect::DrawRect(float left, float top, float right, float bottom, SkColor color)
	: DrawCommand(left, top, right, bottom)
	, color(color)
{}

std::shared_ptr<DrawRect> DrawRect::createLTRB(float left, float top, float right, float bottom, SkColor color) {
	return std::make_shared<DrawRect>(left, top, right, bottom, color);
}

void DrawRect::execute(float scroll, SkCanvas& canvas) {
	SkRect rect = SkRect::MakeLTRB(left, top - scroll, right, bottom - scroll);
	SkPaint paint;
	paint.setColor(color);
	canvas.drawRect(rect, paint);
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
	m_fonts[info] = font;
	return m_fonts[info];
}

LayoutBase::LayoutBase(std::vector<std::shared_ptr<Node>> nodes, std::weak_ptr<LayoutBase> parent)
	: m_nodes(nodes)
	, m_parent(parent)
{}

void LayoutBase::print_layout(int indent) {
	for (int i = 0; i < indent; i++) {
		std::cout << " ";
	}
	std::cout << "{x:" << m_x << ",y:" << m_y << ",w:" << m_width << ",h:" << m_height << "} ";

	for (auto node : m_nodes) {
		if (node->type == NodeType::Text) {
			auto text = static_cast<Text const&>(*node);
			std::cout << text.text << std::endl;;
		} else if (node->type == NodeType::Element) {
			auto tag = static_cast<Element const&>(*node);
			std::cout << "<" << tag.tag << ">" << std::endl;;
		}
	}

	for (auto const& child : m_children) {
		child->print_layout(indent + 2);
	}
}

constexpr std::array TEXT_LIKE_ELEMENTS = { "i", "b", "strong", "em", "small", "sub", "sup", "ins", "del", "mark" };

BlockLayout::BlockLayout(std::vector<std::shared_ptr<Node>> nodes, std::shared_ptr<LayoutBase> parent, std::weak_ptr<BlockLayout> previous)
	: LayoutBase(nodes, parent)
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

constexpr std::array BLOCK_ELEMENTS = {
	"html", "body", "article", "section", "nav", "aside",
	"h1", "h2", "h3", "h4", "h5", "h6", "hgroup", "header",
	"footer", "address", "p", "hr", "pre", "blockquote",
	"ol", "ul", "menu", "li", "dl", "dt", "dd", "figure",
	"figcaption", "main", "div", "table", "form", "fieldset",
	"legend", "details", "summary"
};

LayoutMode BlockLayout::layout_mode() const {
	if (std::ranges::all_of(m_nodes, [](std::shared_ptr<Node> n) { return n->type == NodeType::Text; })) {
		return LayoutMode::Inline;
	}
	for (auto const& node : m_nodes) {
		for (auto const& child : node->children) {
			if (child->type == NodeType::Element) {
				auto element = static_cast<Element const&>(*child);
				if (std::find(BLOCK_ELEMENTS.begin(), BLOCK_ELEMENTS.end(), element.tag) != BLOCK_ELEMENTS.end()) {
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

constexpr float LI_BULLET_SPACING = 3 * HSTEP;

void BlockLayout::layout(FontCache& font_cache) {
	m_x = m_parent.lock()->m_x;
	m_width = m_parent.lock()->m_width;
	if (m_nodes[0]->type == NodeType::Element) {
		if (auto element = static_cast<Element const&>(*m_nodes[0]); element.tag == "li") {
			assert(m_nodes.size() == 1 && "layout isn't shared between li");
			m_x += LI_BULLET_SPACING;
			m_width -= LI_BULLET_SPACING;
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
		assert(m_nodes.size() == 1 && "block layout can't have multiple children");
		std::shared_ptr<LayoutBase> shared = shared_from_this();
		std::weak_ptr<BlockLayout> previous;
		std::vector<std::shared_ptr<Node>> nodes;
		for (auto const& child : m_nodes[0]->children) {
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
		if (!nodes.empty()) {
			auto next = std::make_shared<BlockLayout>(nodes, shared, previous);
			m_children.push_back(next);
		}
	} else if (mode == LayoutMode::Inline) {
		for (auto const& node : m_nodes) {
			recurse(*node, font_cache);
		}
		flush();
	} else {
		assert(false && "unreachable");
	}

	for (auto child : m_children) {
		child->layout(font_cache);
	}

	if (mode == LayoutMode::Block) {
		m_height = std::transform_reduce(m_children.begin(), m_children.end(), 0.0, std::plus<>{}, [](auto const& c) { return c->m_height; });
	} else if (mode == LayoutMode::Inline) {
		m_height = m_cursor_y;
	} else {
		assert(false && "unreachable");
	}
}

void BlockLayout::paint(std::vector<std::shared_ptr<DrawCommand>>& commands) const {
	for (auto const& node : m_nodes) {
		if (node->type == NodeType::Element) {
			auto element = static_cast<Element const&>(*node);
			if (element.tag == "nav") {
				// todo: change all finds to contains if appropriate
				// todo: implement class selectors?
				/*
				if (auto class_ = element.attributes.find("class"); class_ != element.attributes.end() && class_->second == "links") {
					std::shared_ptr<DrawCommand> rect = DrawRect::createLTRB(m_x, m_y, m_x + m_width, m_y + m_height, *parse_color("lightgray"));
					commands.push_back(rect);
				}
				*/
			}

			auto bgcolor_iter = element.styles.find("background-color");
			std::string bgcolor = (bgcolor_iter != element.styles.end()) ? bgcolor_iter->second : std::string{ "transparent" };
			if (bgcolor != "transparent") {
				auto color = parse_color(bgcolor);
				if (color) {
					auto rect = DrawRect::createLTRB(m_x, m_y, m_x + m_width, m_y + m_height, *color);
					commands.push_back(rect);
				}
			}

			if (element.tag == "li") {
				constexpr float LI_BULLET_SIZE = ((float)VSTEP) / 3.0;
				constexpr float VERTICAL_SPACING = VSTEP - LI_BULLET_SIZE;
				constexpr float HORIZONTAL_SPACING = LI_BULLET_SPACING - LI_BULLET_SIZE;
				std::shared_ptr<DrawCommand> rect = DrawRect::createLTRB(
					m_x - 0.5 * HORIZONTAL_SPACING - LI_BULLET_SIZE,
					m_y + 0.5 * VERTICAL_SPACING,
					m_x - 0.5 * HORIZONTAL_SPACING,
					m_y + 0.5 * VERTICAL_SPACING + LI_BULLET_SIZE,
					SK_ColorBLACK);
				commands.push_back(rect);
			}
		}
	}
	for (auto const& pos : m_display_list) {
		auto cmd = DrawText::create(pos.left, pos.top, pos.width, pos.text, pos.font, pos.color);
		commands.push_back(cmd);
	}
}

void BlockLayout::recurse(Node const& node, FontCache& font_cache) {
	if (node.type == NodeType::Text) {
		Text const& text = static_cast<Text const&>(node);

		bool is_bold = false;
		if (node.styles.find("font-weight")->second == "bold") {
			is_bold = true;
		}

		bool is_italic = false;
		if (node.styles.find("font-style")->second == "italic") {
			is_italic = true;
		}

		std::string_view size_str { node.styles.find("font-size")->second };
		size_t size = std::stoi(std::string{size_str.substr(0, size_str.size() - 2)});

		SkFont &font = font_cache.get_font(size, is_bold, is_italic);
		std::vector<std::string_view> words = split_on_any(text.text);
		for (auto w : words) {
			w = trim_whitespace(w);
			if (w != "") {
				word(w, node, font);
			}
		}
	} else if (node.type == NodeType::Element) {
		Element const& element = static_cast<Element const&>(node);
		if (element.tag == "br") {
			flush();
		}
		for (auto const& child : node.children) {
			recurse(*child, font_cache);
		}
		if (element.tag == "p") {
			// todo: move to css?
			m_cursor_y += VSTEP;
		}
	} else {
		assert(false && "unreachable");
	}
}

void BlockLayout::word(std::string_view word, Node const& node, SkFont& font) {
	std::string_view color_str = node.styles.find("color")->second;
	// todo: discard styling if the color fails to parse
	// this will need to be in the cascade i think
	SkColor color = parse_color(color_str).value_or(SK_ColorBLACK);

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
			auto pos = StringPosition {
				.left = m_cursor_x,
				.width = w,
				.text = std::move(word_to_render),
				.font = font,
				.is_super_text = m_in_sup,
				.color = color,
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

void BlockLayout::flush() {
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
			pos.top = baseline - max_ascent / 2;
		} else {
			pos.top = baseline;
		}
	}

	// todo: reintroduce support for centering and right aligning text
	/*
	if (m_in_title || right_align) {
		auto const& w = m_line.back();
		float right_side_gap = (float) m_width - w.left - w.width - (float) HSTEP;

		float change = (m_in_title) ? right_side_gap / 2 : right_side_gap;
		for (auto &pos : m_line) {
			pos.left += change;
		}
	}
	*/

	auto descents = std::views::transform(metrics, &SkFontMetrics::fDescent);
	float max_descent = std::ranges::max(descents);

	// make positions relative
	for (auto& pos : m_line) {
		pos.left += m_x;
		pos.top += m_y;
	}

	// todo: metrics.fLeading
	m_cursor_y = baseline + max_descent * 1.25;
	m_cursor_x = 0;
	m_display_list.insert(m_display_list.end(), m_line.begin(), m_line.end());
	m_line = {};
}

DocumentLayout::DocumentLayout(std::shared_ptr<Node> node, FontCache& font_cache, int screen_width)
	: LayoutBase(std::vector{node}, std::weak_ptr<LayoutBase>())
	, m_font_cache(font_cache)
	, m_screen_width(screen_width)
{}

void DocumentLayout::layout() {
	std::shared_ptr<LayoutBase> shared = shared_from_this();
	assert(m_nodes.size() == 1);
	auto child = std::make_shared<BlockLayout>(m_nodes, shared, std::weak_ptr<BlockLayout>());
	m_children.push_back(child);
	m_x = HSTEP;
	m_y = VSTEP;
	m_width = m_screen_width - 2 * HSTEP;
	child->layout(m_font_cache);
	m_height = child->m_height;
}

void DocumentLayout::paint(std::vector<std::shared_ptr<DrawCommand>>&) const {}

void paint_tree(LayoutBase const& layout, std::vector<std::shared_ptr<DrawCommand>>& commands) {
	layout.paint(commands);

	for (auto child : layout.m_children) {
		paint_tree(*child, commands);
	}
}
