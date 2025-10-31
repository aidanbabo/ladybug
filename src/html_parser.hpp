#pragma once
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class NodeType {
	Tag,
	Text,
};

struct Node {
	std::vector<std::shared_ptr<Node>> children;
	std::weak_ptr<Node> parent;
	NodeType type;

	Node(std::weak_ptr<Node> parent, NodeType type);
};

struct Text : public Node  {
	std::string text;

	Text(std::weak_ptr<Node> parent, std::string text);
};

struct Tag : public Node  {
	std::string tag;
	std::unordered_map<std::string, std::string> attributes;

	Tag(std::weak_ptr<Node> parent, std::string tag, std::unordered_map<std::string, std::string> attributes);
};

class HTMLParser {
	std::string m_body;
	std::vector<std::shared_ptr<Tag>> m_unfinished;

public:
	explicit HTMLParser(std::string body);
	std::shared_ptr<Node> parse();
private:
	void add_text(std::string text);
	std::pair<std::string, std::unordered_map<std::string, std::string>> get_attributes(std::string_view text);
	void add_tag(std::string tag);
	void implicit_tags(std::optional<std::string_view>);
	std::shared_ptr<Node> finish();
};

void print_node(Node const& node, int indent = 0);
