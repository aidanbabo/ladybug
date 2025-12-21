#pragma once

#include <cstdint>
#include <optional>
#include <string>

std::string url_encode(std::string_view s);

struct URL {
	bool view_source;
	std::string scheme;
	std::string host;
	uint16_t port;
	std::string path;
	std::string fragment;

	static URL ABOUT_BLANK;

	static std::optional<URL> create(std::string_view string);
	std::optional<URL> resolve(std::string_view url) const;
	bool equal_disregarding_fragment(URL const& other) const;
	std::string origin() const;


	bool operator==(const URL& other) const noexcept;
// todo: i want these to be module-private
	URL reusable_connection_subsection() const;
	URL cachable_subsection() const;
};

std::ostream& operator<<(std::ostream& os, URL const& url);

template <>
struct std::hash<URL> {
	std::size_t operator()(const URL& u) const noexcept;
};

