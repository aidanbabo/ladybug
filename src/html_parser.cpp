#include "html_parser.hpp"
#include "utils.hpp"

#include <array>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>

constexpr std::array SELF_CLOSING_TAGS = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr" };
constexpr std::array HEAD_TAGS = { "base", "basefont", "bgsound", "noscript", "link", "meta", "title", "style", "script" };

static void unescape_sequence(std::string_view body, size_t &i, std::string &out) {
	// todo: allow semicolons or no semicolons
	struct EscapeSequence {
		std::string_view sequence;
		std::string_view replacement;
	};
	constexpr std::array escapes = std::to_array<EscapeSequence>({
		{"lt;", "<"},
		{"gt;", ">"},
		{"amp;", "&"},
		{"quot;", "\""},
		{"shy", SOFT_HYPHEN},
	});
	for (auto escape : escapes) {
		if (body.compare(i, escape.sequence.size(), escape.sequence) == 0) {
			i += escape.sequence.size();
			out += escape.replacement;
			return;
		}
	}
	out.push_back('&');
}

Node::Node(std::weak_ptr<Node> parent, NodeType type) : children(), parent(parent), type(type) {}

Text::Text(std::weak_ptr<Node> parent, std::string text) : Node(parent, NodeType::Text), text(text) {}

Tag::Tag(std::weak_ptr<Node> parent, std::string tag, std::unordered_map<std::string, std::string> attributes) 
	: Node(parent, NodeType::Tag)
	  , tag(tag)
	  , attributes(attributes)
{}

HTMLParser::HTMLParser(std::string body)
	: m_body(body)
	, m_unfinished()
{}

void HTMLParser::implicit_tags(std::optional<std::string_view> tag_) {
	std::string_view tag = tag_.value_or("");
	for (;;) {
		// todo: no need to collect
		std::vector<std::string_view> open_tags(m_unfinished.size());
		// Direct list initialization prevents the std::string from being copied to the output buffer before a conversion to std::string_view.
		// Not preventing this means std::string_view would be dangling.
		std::transform(m_unfinished.begin(), m_unfinished.end(), open_tags.begin(), [](std::shared_ptr<Tag> t) { return std::string_view{t->tag}; });

		if (open_tags.empty() && tag != "html") {
			add_tag("html");
		} else if (open_tags.size() == 1 && open_tags[0] == "html" && tag != "head" && tag != "body" && tag != "/html") {
			if (std::find(HEAD_TAGS.begin(), HEAD_TAGS.end(), tag) != HEAD_TAGS.end()) {
				add_tag("head");
			} else {
				add_tag("body");
			}
		// todo: deep vector equals?
		} else if (open_tags.size() == 2 && open_tags[0] == "html" && open_tags[1] == "head" && tag != "/head" && std::find(HEAD_TAGS.begin(), HEAD_TAGS.end(), tag) == HEAD_TAGS.end()) {
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
	std::vector<std::string_view> parts = split_on_any(text);
	std::string tag { parts[0] };
	// casefold :( localization
	std::transform(tag.begin(), tag.end(), tag.begin(), [](char c) { return std::tolower(c); });

	std::unordered_map<std::string, std::string> attributes;
	for (size_t i = 1; i < parts.size(); i++) {
		std::string_view part = parts[i];
		if (std::find(part.begin(), part.end(), '=') != part.end()) {
			std::vector<std::string_view> pair = split(parts[i], "=", 1);
			std::string key { pair[0] };
			std::transform(key.begin(), key.end(), key.begin(), [](char c) { return std::tolower(c); });
			std::string value { pair[1] };

			if (value.size() > 2 && (value[0] == '\'' || value[0] == '"')) {
				value.erase(0, 1);
				value.pop_back();
			}

			attributes.insert({key, value});
		} else {
			std::string key { part };
			std::transform(key.begin(), key.end(), key.begin(), [](char c) { return std::tolower(c); });
			attributes.insert({key, ""});
		}
	}
	return { tag, attributes };
}

std::optional<std::shared_ptr<Tag>> HTMLParser::add_tag(std::string tag_) {
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
		// todo: why aren't these one step?
		std::shared_ptr<Tag> node = m_unfinished.back();
		m_unfinished.pop_back();
		// todo: check it's the right tag?

		assert(!m_unfinished.empty());
		std::shared_ptr<Node> parent = m_unfinished.back();
		parent->children.push_back(node);
		return node;
	} else if (std::find(SELF_CLOSING_TAGS.begin(), SELF_CLOSING_TAGS.end(), tag) != SELF_CLOSING_TAGS.end()) {
		assert(!m_unfinished.empty());
		std::shared_ptr<Node> parent = m_unfinished.back();
		std::shared_ptr<Tag> node = std::make_shared<Tag>(parent, tag, attributes);
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
			// todo: return something here? is this valuable?
		}
		std::shared_ptr<Node> parent = m_unfinished.empty() ? nullptr : m_unfinished.back();
		std::shared_ptr<Tag> node = std::make_shared<Tag>(parent, tag, attributes);
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
	while (i < m_body.length()) {
		if (in_script) {
			std::string_view script_end = "</script>";
			size_t script_end_start = m_body.find(script_end, i);
			assert(script_end_start != std::string::npos);
			add_tag(std::string{ script_end });
			i = script_end_start + script_end.size();
			in_script = false;
		}
		char c = m_body[i++];
		if (c == '<') {
			std::string_view comment_start = "!--";
			if (m_body.compare(i, comment_start.size(), comment_start.data()) == 0) {
				std::string_view comment_end = "-->";
				size_t comment_end_start = m_body.find(comment_end, i + comment_start.size());
				assert(comment_end_start != std::string::npos);
				i = comment_end_start + comment_end.size();
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
				if (auto tag = add_tag(buffer); tag) {
					if (tag.value()->tag == "script") {
						in_script = true;
					}
				}
			}
			buffer = "";
		} else if (!in_tag && c == '&') {
			unescape_sequence(m_body, i, buffer);
		} else { 
			buffer.push_back(c);
		}
	}
	// todo: handle in_script

	if (!in_tag && !buffer.empty()) {
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
		std::cout << text.text << std::endl;;
	} else if (node.type == NodeType::Tag) {
		auto tag = static_cast<Tag const&>(node);
		std::cout << "<" << tag.tag << ">" << std::endl;;
	}

	for (auto const& child : node.children) {
		print_node(*child, indent + 2);
	}
}
