#pragma once
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class NodeType {
	Element,
	Text,
};

struct StyleSheet;

struct Node {
	std::vector<std::shared_ptr<Node>> children;
	std::weak_ptr<Node> parent;
	NodeType type;
	std::unordered_map<std::string, std::string> styles{};

	Node(std::weak_ptr<Node> parent, NodeType type);
	void style(StyleSheet const& rules);
};


struct Selector {
	size_t priority;

	Selector(size_t priority);
	virtual bool matches(Node const& node) = 0;
	virtual ~Selector() = default;
};

struct StyleSheet {
	// using unique_ptr means the worse template errors imaginable and i am not comfortable knowing all the
	// silly implicity copy rules of this language to figure it out
	std::vector<std::pair<std::shared_ptr<Selector>, std::unordered_map<std::string, std::string>>> rules;
};

struct Text : public Node  {
	std::string text;

	Text(std::weak_ptr<Node> parent, std::string text);
};

struct Element : public Node  {
	std::string tag;
	std::unordered_map<std::string, std::string> attributes;

	Element(std::weak_ptr<Node> parent, std::string tag, std::unordered_map<std::string, std::string> attributes);
};

class HTMLParser {
	std::string m_body;
	std::vector<std::shared_ptr<Element>> m_unfinished;

public:
	explicit HTMLParser(std::string body);
	std::shared_ptr<Node> parse();
private:
	void add_text(std::string text);
	std::pair<std::string, std::unordered_map<std::string, std::string>> get_attributes(std::string_view text);
	std::optional<std::shared_ptr<Element>> add_tag(std::string tag);
	void implicit_tags(std::optional<std::string_view>);
	std::shared_ptr<Node> finish();
};

void print_node(Node const& node, int indent = 0);

struct TagSelector : public Selector {
	std::string tag;

	TagSelector(std::string tag);
	bool matches(Node const& node) override;
};

struct DescendantSelector : public Selector {
	// todo: figure out unique_ptr
	// The problem is that we can't copy this structure (StyleSheet) ever if there are unique_ptrs
	// which is probably correct, but it is hard to deal with the compiler errors that complain about it
	// Explicitly deleting constructors and assignment operators helps a little bit but it is a big pain.
	std::shared_ptr<Selector> ancestor;
	std::shared_ptr<Selector> descendant;

	DescendantSelector(std::shared_ptr<Selector> ancestor, std::shared_ptr<Selector> descendant);
	bool matches(Node const& node) override;
};

struct ClassSelector : public Selector {
	std::string class_;

	ClassSelector(std::string class_);
	bool matches(Node const& node) override;
};

class CSSParser {
	std::string m_s;
	size_t m_i;
public:
	CSSParser(std::string s);
	std::optional<std::unordered_map<std::string, std::string>> body();
	// LMAO: lets make this shorter
	std::optional<StyleSheet> parse();
private:
	void whitespace();
	std::optional<std::string> word();
	[[nodiscard]] bool literal(char literal);
	std::optional<std::pair<std::string, std::string>> pair();
	std::optional<char> ignore_until_any(std::string_view chars);
	std::optional<std::shared_ptr<Selector>> selector();
};
