#include "utils.hpp"

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

// todo: make more generic
std::vector<std::string_view> split(std::string_view s, std::string_view delimiter, int nsplits) {
	std::vector<std::string_view> items;
	size_t start = 0;
	// todo: make smaller
	for (;;) {
		size_t end_pos = s.find(delimiter, start);
		if (end_pos == std::string_view::npos || (int) items.size() == nsplits) {
			std::string_view item = s.substr(start);
			items.push_back(item);
			return items;
		}

		std::string_view item = s.substr(start, end_pos - start);
		items.push_back(item);
		start = end_pos + delimiter.length();
	}
}

std::vector<std::string_view> split_on_any(std::string_view s, std::string_view delimiters, int nsplits) {
	std::vector<std::string_view> items;
	size_t start = 0;
	for (;;) {
		size_t end_pos = s.find_first_of(delimiters, start);
		if (end_pos == std::string_view::npos || (int) items.size() == nsplits) {
			std::string_view item = s.substr(start);
			items.push_back(item);
			return items;
		}

		std::string_view item = s.substr(start, end_pos - start);
		items.push_back(item);
		start = end_pos + 1;
	}
}
