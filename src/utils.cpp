#include "utils.hpp"

// from Boost
void combine_hash(size_t &seed, size_t value) {
	seed ^= (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

std::string trim_whitespace(std::string s) {
	char const *whitespace = " \t\n\r\f\v";
	s.erase(s.find_last_not_of(whitespace) + 1);
	s.erase(0, s.find_first_not_of(whitespace));
	return s;
}

std::vector<std::string> split(std::string s, std::string const& delimiter, int nsplits) {
	std::vector<std::string> items;
	size_t start = 0;
	for (;;) {
		size_t end_pos = s.find(delimiter, start);
		if (end_pos == std::string::npos || (int) items.size() == nsplits) {
			std::string item = s.substr(start);
			items.push_back(item);
			return items;
		}

		std::string item = s.substr(start, end_pos - start);
		items.push_back(item);
		start = end_pos + delimiter.length();
	}
}

std::vector<std::string> split_on_any(std::string s, std::string const& delimiters, int nsplits) {
	std::vector<std::string> items;
	size_t start = 0;
	for (;;) {
		size_t end_pos = s.find_first_of(delimiters, start);
		if (end_pos == std::string::npos || (int) items.size() == nsplits) {
			std::string item = s.substr(start);
			items.push_back(item);
			return items;
		}

		std::string item = s.substr(start, end_pos - start);
		items.push_back(item);
		start = end_pos + 1;
	}
}
