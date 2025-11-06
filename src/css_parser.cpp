#include <cassert>

#include "parsers.hpp"
#include "utils.hpp"

#include <array>
#include <algorithm>

Selector::Selector(size_t priority)
	: priority(priority)
{}

TagSelector::TagSelector(std::string tag)
	: Selector(1)
	, tag(std::move(tag))
{}

bool TagSelector::matches(Node const& node) {
	if (node.type == NodeType::Element) {
		auto element = static_cast<Element const&>(node);
		return element.tag == tag;
	}
	return false;
}

DescendantSelector::DescendantSelector(std::shared_ptr<Selector> ancestor, std::shared_ptr<Selector> descendant)
	: Selector(ancestor->priority + descendant->priority)
	, ancestor(std::move(ancestor))
	, descendant(std::move(descendant))
{}

bool DescendantSelector::matches(Node const& node) {
	if (!descendant->matches(node)) return false;
	auto parent = node.parent.lock();
	while (parent) {
		if (ancestor->matches(*parent)) return true;
		parent = parent->parent.lock();
	}
	return false;
}

ClassSelector::ClassSelector(std::string class_)
	: Selector(1)
	, class_(class_)
{}

bool ClassSelector::matches(Node const& node) {
	if (node.type != NodeType::Element) {
		return false;
	}
	auto element = static_cast<Element const&>(node);
	if (auto class_attr = element.attributes.find("class"); class_attr != element.attributes.end()) {
		// todo: no need to alloc
		auto classes = split_on_any(class_attr->second);
		for (auto const& c : classes) {
			if (c == class_) {
				return true;
			}
		}
	}
	return false;
}

CSSParser::CSSParser(std::string s)
	: m_s(std::move(s))
	, m_i(0)
{}

void CSSParser::whitespace() {
	while (m_i < m_s.size() && std::isspace(m_s[m_i])) {
		m_i++;
	}
}

std::optional<std::string> CSSParser::word() {
	constexpr std::string_view puncs = "#-.%";
	bool in_quotes = false;
	auto start = m_i;
	std::string buf;
	while (m_i < m_s.size()) {
		if (m_s[m_i] == '\'') {
			in_quotes = !in_quotes;
		} else if (in_quotes) {
			buf.push_back(m_s[m_i]);
		} else if (std::isalnum(m_s[m_i]) || puncs.find(m_s[m_i]) != std::string::npos) {
			buf.push_back(m_s[m_i]);
		} else {
			break;
		}
		m_i++;
	}
	if (!(m_i > start)) {
		return std::nullopt;
	}
	return buf;
}

bool CSSParser::literal(char literal) {
	if (!(m_i < m_s.size() && m_s[m_i] == literal)) {
		return false;
	}
	m_i++;
	return true;
}

std::optional<std::pair<std::string, std::string>> CSSParser::pair() {
	auto prop = word();
	if (!prop) return std::nullopt;
	whitespace();
	if (!literal(':')) return std::nullopt;
	whitespace();
	auto val = word();
	if (!val) return std::nullopt;
	make_lowercase(*prop);
	return std::make_pair(*prop, *val);
}

std::optional<char> CSSParser::ignore_until_any(std::string_view chars) {
	while (m_i < m_s.size()) {
		if (chars.find(m_s[m_i]) != std::string::npos) {
			return m_s[m_i];
		} else {
			m_i++;
		}
	}
	return std::nullopt;
}

bool CSSParser::shorthand_property_extras(std::unordered_map<std::string, std::string>& pairs, std::string prop, std::string first) {
	assert(prop == "font");
	auto style { first };

	auto weight { word() };
	if (!weight) return false;
	whitespace();

	auto size { word() };
	if (!size) return false;
	whitespace();

	auto family { word() };
	if (!family) return false;
	whitespace();

	if (!literal(';')) return false;

	pairs["font-style"] = style;
	pairs["font-weight"] = *weight;
	pairs["font-size"] = *size;
	pairs["font-family"] = *family;

	return true;
}

constexpr std::array<std::string_view, 1> SHORTHAND_PROPERTIES = { "font" };

std::optional<std::unordered_map<std::string, std::string>> CSSParser::body() {
	std::unordered_map<std::string, std::string> pairs;
	while (m_i < m_s.size()) {
		auto p = pair();
		if (!p) goto failed_parse;
		whitespace();
		if (!literal(';')) {
			if (std::find(SHORTHAND_PROPERTIES.begin(), SHORTHAND_PROPERTIES.end(), p->first) != SHORTHAND_PROPERTIES.end()) {
				if (!shorthand_property_extras(pairs, std::move(p->first), std::move(p->second))) {
					goto failed_parse;
				}
			} else {
				goto failed_parse;
			}
		} else {
			pairs[std::move(p->first)] = std::move(p->second);
		}
		whitespace();
		continue;
failed_parse:
		auto why = ignore_until_any(";}");
		if (why.value_or('\0') == ';') {
			assert(literal(';'));
			whitespace();
		} else {
			break;
		}
	}
	return pairs;
}

// todo: element+class selector (e.g. p.large)
std::optional<std::shared_ptr<Selector>> CSSParser::selector() {
	auto class_or_tag_selector = [](std::string w) -> std::shared_ptr<Selector> {
		if (w.starts_with('.')) {
			return std::make_shared<ClassSelector>(w.substr(1));
		} else {
			return std::make_shared<TagSelector>(w);
		}
	};

	auto w = word();
	if (!w) return std::nullopt;
	make_lowercase(*w);
	auto out = class_or_tag_selector(*w);
	whitespace();
	while (m_i < m_s.size() && m_s[m_i] != '{') {
		auto tag = word();
		if (!tag) return std::nullopt;
		make_lowercase(*tag);
		auto descendant = class_or_tag_selector(*tag);
		out = std::make_shared<DescendantSelector>(out, descendant);
		whitespace();
	}
	return out;
}

std::optional<StyleSheet> CSSParser::parse() {
	StyleSheet sheet;
	while (m_i < m_s.size()) {
		whitespace();
		auto sel = selector();
		if (!sel) goto failed_parse;
		if (!literal('{')) goto failed_parse;
		whitespace();
		// crappy goto hack scope
		{
			auto b = body();
			if (!b) goto failed_parse;
			if (!literal('}')) goto failed_parse;
			sheet.rules.push_back(std::make_pair(std::move(*sel), *b));
		}
		continue;
failed_parse:
		auto why = ignore_until_any("}");
		if (why.value_or('\0') == '}') {
			assert(literal('}'));
			whitespace();
		} else {
			break;
		}
	}
	return sheet;
}
