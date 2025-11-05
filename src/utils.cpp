#include <array>

#include "utils.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

// from Boost
void combine_hash(size_t &seed, size_t value) {
	seed ^= (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

std::string_view trim_whitespace(std::string_view s) {
	char const *whitespace = " \t\n\r\f\v";
	size_t start = s.find_first_not_of(whitespace);
	if (start == std::string_view::npos) {
		start = 0;
	}
	size_t end = s.find_last_not_of(whitespace) + 1;
	if (end == std::string_view::npos) {
		end = s.size();
	}
	return s.substr(start, end - start);
}

std::vector<std::string_view> split(std::string_view s, std::string_view delimiter, int nsplits) {
	std::vector<std::string_view> items;
	size_t start = 0;
	size_t end_pos;
	do {
		end_pos = s.find(delimiter, start);
		if ((int) items.size() == nsplits) {
			end_pos = std::string_view::npos;
		}

		std::string_view item = s.substr(start, end_pos - start);
		items.push_back(item);
		start = end_pos + delimiter.length();

	} while (end_pos != std::string_view::npos);
	return items;
}

std::vector<std::string_view> split_on_any(std::string_view s, std::string_view delimiters, int nsplits) {
	std::vector<std::string_view> items;
	size_t start = 0;
	size_t end_pos;
	do {
		end_pos = s.find_first_of(delimiters, start);
		if ((int) items.size() == nsplits) {
			end_pos = std::string_view::npos;
		}

		std::string_view item = s.substr(start, end_pos - start);
		items.push_back(item);
		start = end_pos + 1;

	} while (end_pos != std::string_view::npos);
	return items;
}

// todo: https://html.spec.whatwg.org/multipage/named-characters.html
struct EscapeSequence {
	std::string_view sequence;
	std::string_view replacement;
};

constexpr std::array ESCAPES = std::to_array<EscapeSequence>({
	{"lt;", "<"},
	{"gt;", ">"},
	{"amp;", "&"},
	{"quot;", "\""},
	{"shy", SOFT_HYPHEN},
});

std::string escape(std::string_view source) {
	std::string output;
	for (size_t i = 0; i < source.size(); i++) {
		for (auto escape : ESCAPES) {
			if (source.compare(i, escape.replacement.size(), escape.replacement) == 0) {
				output.push_back('&');
				output.append(escape.sequence);
				goto outer_loop_end;
			}
		}
		output.push_back(source[i]);
outer_loop_end:
		;
	}
	return output;
}

void unescape_sequence(std::string_view source, size_t &offset, std::string &output_buffer) {
	for (auto escape : ESCAPES) {
		if (source.compare(offset, escape.sequence.size(), escape.sequence) == 0) {
			offset += escape.sequence.size();
			output_buffer += escape.replacement;
			return;
		}
	}
	output_buffer.push_back('&');
}

void make_lowercase(std::string& s) {
	std::transform(s.begin(), s.end(), s.begin(), [](char c) { return std::tolower(c); });
}

std::optional<std::string> read_entire_file_to_string(std::string const& path) {
	std::ifstream file(path);
	if (!file.is_open()) {
		std::cerr << "Invalid file path: " << path << std::endl;
		return std::nullopt;
	}

	std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();
	return content;
}
