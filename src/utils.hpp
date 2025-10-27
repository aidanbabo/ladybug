#pragma once
#include <string>
#include <vector>

inline constexpr char const* SOFT_HYPHEN = "­";
inline constexpr int const HSTEP = 13;
inline constexpr int const VSTEP = 18;

void combine_hash(size_t &seed, size_t value);
std::string trim_whitespace(std::string s);
std::vector<std::string> split(std::string s, std::string const& delimiter, int nsplits = -1);
std::vector<std::string> split_on_any(std::string s, std::string const& delimiters, int nsplits = -1);
