#include <cassert>

#include "parsers.hpp"
#include "utils.hpp"

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
	auto start = m_i;
	while (m_i < m_s.size()) {
		if (std::isalnum(m_s[m_i]) || puncs.find(m_s[m_i]) != std::string::npos) {
			m_i++;
		} else {
			break;
		}
	}
	if (!(m_i > start)) {
		return std::nullopt;
	}
	return m_s.substr(start, m_i - start);
}

bool CSSParser::literal(char literal) {
	if (!(m_i < m_s.size()) && m_s[m_i] == literal) {
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

std::optional<std::unordered_map<std::string, std::string>> CSSParser::body() {
	std::unordered_map<std::string, std::string> pairs;
	while (m_i < m_s.size()) {
		auto p = pair();
		if (!p) goto failed_parse;
		pairs[std::move(p->first)] = std::move(p->second);
		whitespace();
		if (!literal(';')) goto failed_parse;
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

std::optional<std::shared_ptr<Selector>> CSSParser::selector() {
	auto w = word();
	if (!w) return std::nullopt;
	make_lowercase(*w);
	std::shared_ptr<Selector> out = std::make_shared<TagSelector>(*w);
	whitespace();
	while (m_i < m_s.size() && m_s[m_i] != '{') {
		auto tag = word();
		if (!tag) return std::nullopt;
		make_lowercase(*tag);
		auto descendant = std::make_shared<TagSelector>(*tag);
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
