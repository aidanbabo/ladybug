#include "parsers.hpp"
#include "utils.hpp"

#include <array>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>

constexpr std::array SELF_CLOSING_TAGS = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr" };
constexpr std::array HEAD_TAGS = { "base", "basefont", "bgsound", "noscript", "link", "meta", "title", "style", "script" };

Node::Node(std::weak_ptr<Node> parent, NodeType type)
	: children()
	, parent(parent)
	, type(type)
{}

Text::Text(std::weak_ptr<Node> parent, std::string text)
	: Node(parent, NodeType::Text)
	, text(text)
{}

Element::Element(std::weak_ptr<Node> parent, std::string tag, std::unordered_map<std::string, std::string> attributes)
	: Node(parent, NodeType::Element)
	, tag(tag)
	, attributes(attributes)
{}

static const std::unordered_map<std::string_view, std::string_view> INHERITIED_PROPERTIES = {
	{ "font-size", "16px" },
	{ "font-style", "normal" },
	{ "font-weight", "normal" },
	{ "color", "black" },
	{ "font-family", "times" },
};

void Node::style(StyleSheet const& sheet) {
	for (auto const& [property, default_value] : INHERITIED_PROPERTIES) {
		if (auto p = parent.lock()) {
			styles[std::string{property}] = p->styles[std::string{property}];
		} else {
			styles[std::string{property}] = std::string{default_value};
		}
	}
	for (auto const& [selector, body] : sheet.rules) {
		if (!selector->matches(*this)) continue;
		for (auto [p, v] : body) {
			styles[p] = v;
		}
	}
	if (type == NodeType::Element) {
		auto element = static_cast<Element const&>(*this);
		if (auto styles_text = element.attributes.find("style"); styles_text != element.attributes.end()) {
			auto pairs = CSSParser(styles_text->second).body();
			if (pairs) {
				for (auto [p, v] : *pairs) {
					styles[p] = v;
				}
			}
		}
	}

	if (styles["font-size"].ends_with("%")) {
		std::string size;
		if (auto p = parent.lock()) {
			size = p->styles["font-size"];
		} else {
			// if root, we do default font size
			size = std::string{INHERITIED_PROPERTIES.find("font-size")->second};
		}
		auto fsize_str = styles["font-size"];
		// chop of %
		auto node_pct = std::stof(fsize_str.substr(0, fsize_str.size() - 1)) / 100.0;
		// chop of px
		auto parent_px = std::stof(size.substr(0, size.size() - 2));
		styles["font-size"] = std::to_string(node_pct * parent_px) + "px";
	}

	for (auto& child : children) {
		child->style(sheet);
	}
}

HTMLParser::HTMLParser(std::string body)
	: m_body(body)
	, m_unfinished()
{}

void HTMLParser::implicit_tags(std::optional<std::string_view> tag_) {
	std::string_view tag = tag_.value_or("");
	for (;;) {
		if (m_unfinished.empty() && tag != "html") {
			add_tag("html");
		} else if (m_unfinished.size() == 1 && m_unfinished[0]->tag == "html" && tag != "head" && tag != "body" && tag != "/html") {
			if (std::find(HEAD_TAGS.begin(), HEAD_TAGS.end(), tag) != HEAD_TAGS.end()) {
				add_tag("head");
			} else {
				add_tag("body");
			}
		} else if (m_unfinished.size() == 2 && m_unfinished[0]->tag == "html" && m_unfinished[1]->tag == "head" && tag != "/head" && std::find(HEAD_TAGS.begin(), HEAD_TAGS.end(), tag) == HEAD_TAGS.end()) {
			add_tag("/head");
		} else {
			break;
		}
	}
}

void HTMLParser::add_text(std::string text) {
	bool is_whitespace = std::accumulate(text.begin(), text.end(), true, [](bool a, unsigned char c) { return a &= (std::isspace(c) != 0); });
	if (is_whitespace) {
		return;
	}

	implicit_tags(std::nullopt);
	assert(!m_unfinished.empty());

	std::shared_ptr<Node> parent = m_unfinished.back();
	std::shared_ptr<Node> node = std::make_shared<Text>(parent, text);
	parent->children.push_back(node);
}

std::pair<std::string, std::unordered_map<std::string, std::string>> HTMLParser::get_attributes(std::string_view text) {
	std::vector<std::string_view> parts = split_on_any(text, " \t\r\n\f\v", 1);
	std::string tag { parts[0] };
	// casefold :( localization
	make_lowercase(tag);

	bool in_quotes = false;
	bool parsing_prop = true;
	std::string buf;
	std::string prop;
	std::unordered_map<std::string, std::string> attributes;
	std::string_view remainder = parts.size() > 1 ? parts[1] : std::string_view{};
	for (char c : remainder) {
		if (c == '"') {
			in_quotes = !in_quotes;
		} else if (in_quotes) {
			buf.push_back(c);
		} else if (c == '=') {
			prop = buf;
			buf.clear();
			parsing_prop = false;
		} else if (std::isspace(c)) {
			parsing_prop = true;
			make_lowercase(prop);

			attributes[prop] = buf;
			prop.clear();
			buf.clear();
		} else {
			buf.push_back(c);
		}
	}
	assert(!in_quotes);
	if (parsing_prop) {
		prop = buf;
		buf.clear();
	}
	if (!prop.empty()) {
		attributes[prop] = buf;
	}

	return { tag, attributes };
}

std::optional<std::shared_ptr<Element>> HTMLParser::add_tag(std::string tag_) {
	auto [tag, attributes] = get_attributes(tag_);

	if (tag.starts_with("!")) {
		return std::nullopt;
	}
	implicit_tags(tag);

	if (tag.starts_with("/")) {
		// last tag doesn't have a parent
		if (m_unfinished.size() == 1) {
			return std::nullopt;
		}
		std::shared_ptr<Element> node = m_unfinished.back();
		m_unfinished.pop_back();
		if (node->tag != tag.substr(1)) {
			std::cerr << "closing " << node->tag << " with non-matching closing tag " << tag.substr(1) << std::endl;
			// close anyways
		}

		assert(!m_unfinished.empty());
		std::shared_ptr<Node> parent = m_unfinished.back();
		parent->children.push_back(node);
		return node;
	} else if (std::find(SELF_CLOSING_TAGS.begin(), SELF_CLOSING_TAGS.end(), tag) != SELF_CLOSING_TAGS.end()) {
		assert(!m_unfinished.empty());
		std::shared_ptr<Node> parent = m_unfinished.back();
		std::shared_ptr<Element> node = std::make_shared<Element>(parent, tag, attributes);
		parent->children.push_back(node);
		return node;
	} else {
		if ((tag == "p" && !m_unfinished.empty() && m_unfinished.back()->tag == "p")
		|| (tag == "li" && !m_unfinished.empty() && m_unfinished.back()->tag == "li")) {
			// close
			std::shared_ptr<Node> node = m_unfinished.back();
			m_unfinished.pop_back();

			assert(!m_unfinished.empty());
			std::shared_ptr<Node> parent = m_unfinished.back();
			parent->children.push_back(node);
			// todo: It remains to bee see if returning something here is valuable.
			// Currently, the return value is only used to check if we parsed an opening script tag
			// so that we can shift the parser into a different state, in which case we don't care about closing
			// <p> or <li> tags.
		}
		std::shared_ptr<Node> parent = m_unfinished.empty() ? nullptr : m_unfinished.back();
		std::shared_ptr<Element> node = std::make_shared<Element>(parent, tag, attributes);
		m_unfinished.push_back(node);
		return node;
	}
}

std::shared_ptr<Node> HTMLParser::finish() {
	if (m_unfinished.empty()) {
		implicit_tags(std::nullopt);

	}
	while (m_unfinished.size() > 1) {
		std::shared_ptr<Node> node = m_unfinished.back();
		m_unfinished.pop_back();
		std::shared_ptr<Node> parent = m_unfinished.back();
		parent->children.push_back(node);
	}
	std::shared_ptr<Node> root = m_unfinished.back();
	m_unfinished.pop_back();
	return root;
}

std::shared_ptr<Node> HTMLParser::parse() {
	std::string buffer;
	size_t i = 0;

	bool in_tag = false;
	bool in_script = false;
	bool in_comment = false;
	bool in_quoted_attribute = false;
	while (i < m_body.length()) {
		if (in_script) {
			std::string_view script_end = "</script>";
			size_t script_end_start = m_body.find(script_end, i);
			add_tag(std::string{ "/script" });
			// Out of bounds is ok.
			i = script_end_start + script_end.size();
			in_script = false;
			continue;
		}
		if (in_comment) {
			std::string_view comment_end = "-->";
			size_t comment_end_start = m_body.find(comment_end, i);
			// Out of bounds is ok.
			i = comment_end_start + comment_end.size();
			in_comment = false;
			continue;
		}

		char c = m_body[i++];
		if (in_quoted_attribute) {
			if (c == '"') {
				in_quoted_attribute = false;
			}
			buffer.push_back(c);
		} else if (c == '<') {
			std::string_view comment_start = "!--";
			if (m_body.compare(i, comment_start.size(), comment_start.data()) == 0) {
				i += comment_start.size();
				in_comment = true;
			} else {
				in_tag = true;
				if (!buffer.empty()) {
					add_text(buffer);
				}
				buffer = "";
			}
		} else if (c == '>') {
			in_tag = false;
			if (!buffer.empty()) {
				if (auto tag = add_tag(buffer)) {
					if (tag.value()->tag == "script" && tag.value()->attributes.find("src") == tag.value()->attributes.end()) {
						in_script = true;
					}
					// todo: add similar parsing fallthrough for style tags
				}
			}
			buffer = "";
		} else if (!in_tag && c == '&') {
			unescape_sequence(m_body, i, buffer);
		} else if (in_tag && c == '"') {
			in_quoted_attribute = true;
			buffer.push_back(c);
		} else { 
			buffer.push_back(c);
		}
	}

	if (!in_script && !in_comment && !in_tag && !buffer.empty()) {
		add_text(buffer);
	}
	return finish();
}

void print_node(Node const& node, int indent) {
	for (int i = 0; i < indent; i++) {
		std::cout << " ";
	}
	if (node.type == NodeType::Text) {
		auto text = static_cast<Text const&>(node);
		std::cout << text.text;
	} else if (node.type == NodeType::Element) {
		auto tag = static_cast<Element const&>(node);
		std::cout << "<" << tag.tag;
		for (auto const& attr : tag.attributes) {
			if (attr.first == "style") {
				continue;
			}
			std::cout << " " << attr.first;
			if (!attr.second.empty()) {
				std::cout << "=\"" << attr.second << "\"";
			}
		}
		std::cout << "> ";
	}

	std::cout << "{";
	for (auto const& [prop, value] : node.styles) {
		std::cout << prop << ":" << value << ",";
	}
	std::cout << std::endl;

	for (auto const& child : node.children) {
		print_node(*child, indent + 2);
	}
}
