#pragma once
#include <optional>
#include <string>
#include <vector>

inline constexpr char const* SOFT_HYPHEN = "­";
inline constexpr int const HSTEP = 13;
inline constexpr int const VSTEP = 18;

void combine_hash(size_t &seed, size_t value);
[[nodiscard]] std::string_view trim_whitespace(std::string_view s);
// todo: should these give empty parts?
std::vector<std::string_view> split(std::string_view s, std::string_view delimiter, int nsplits = -1);
std::vector<std::string_view> split_on_any(std::string_view s, std::string_view delimiters = " \t\n\r\f\v", int nsplits = -1);

std::string escape(std::string_view source);
void unescape_sequence(std::string_view source, size_t &offset, std::string &output_buffer);
void make_lowercase(std::string& s);

std::optional<std::string> read_entire_file_to_string(std::string const& path);
