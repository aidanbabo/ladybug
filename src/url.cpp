#include <cassert>
#include <ctime>

#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>

#include "url.hpp"
#include "utils.hpp"

std::string url_encode(std::string_view s) {
	std::string out;
	for (char c : s) {
		if (!std::isalnum(c) && c != '-' && c != '_' && c !='.' && c != '~') {
			out.push_back('%');
			std::stringstream ss;
			ss << std::hex << (int) c;
			std::string t = ss.str();
			std::transform(t.begin(), t.end(), t.begin(), ::toupper);
			out.append(t);
		} else {
			out.push_back(c);
		}
	}
	return out;
}

static std::optional<URL> parse_data_url(bool view_source, std::string scheme, std::string_view string) {
	size_t n = string.find(",");
	if (n == std::string::npos) {
		std::cerr << "Expected ',' in data url" << std::endl;
		return std::nullopt;
	}
	if (string.substr(0, n) != "text/html") {
		std::cerr << "Unsupported MIME type " << string.substr(0, n) << " in URL" << std::endl;
		return std::nullopt;
	}
	// todo: make URL an enum (Rust style) or abstract class (OO style)
	// this isn't really what path is for...
	std::string path(string.substr(n + 1));
	return URL {
		.view_source = view_source,
		.scheme = scheme,
		.host = "",
		.port = 0,
		.path = path,
		.fragment = "",
	};
}

static std::optional<URL> parse_about_url(bool view_source, std::string scheme, std::string_view string) {
	if (string != "blank") {
		std::cerr << "Unsupported about page " << string << " in URL" << std::endl;
		return std::nullopt;
	}
	return URL {
		.view_source = view_source,
		.scheme = scheme,
		.host = "blank",
		.port = 0,
		.path = "",
		.fragment = "",
	};
}

static std::optional<URL> parse_relative_file_url(bool view_source, std::string scheme, std::string_view string) {
	return URL {
		.view_source = view_source,
		.scheme = scheme,
		.host = "",
		.port = 0,
		.path = std::string(string),
		.fragment = "",
	};
}

static std::optional<URL> parse_standard_url(bool view_source, std::string scheme, std::string_view string) {
	std::string host, path, fragment;
	size_t n = string.find('/');
	if (n == std::string::npos) {
		host = string;
		path = "/";
	} else {
		host = string.substr(0, n);
		path = string.substr(n);
	}
	n = path.find('#');
	if (n != std::string::npos) {
		fragment = path.substr(n + 1);
		path = path.substr(0, n);
	}

	n = host.find(":");
	uint16_t port;
	if (n != std::string::npos) {
		port = std::stoi(host.substr(n + 1));
		host = host.substr(0, n);
	} else if (scheme == "https") {
		port = 443;
	} else if (scheme == "http") {
		port = 80;
	} else if (scheme == "file") {
		port = 0;
	} else {
		assert(false && "unreachable");
	}

	if (scheme == "file" && (!host.empty() || port != 0)) {
		std::cerr << "`file` URL should have neither a host or port" << std::endl;
		return std::nullopt;
	}

	return URL {
		.view_source = view_source,
		.scheme = scheme,
		.host = host,
		.port = port,
		.path = path,
		.fragment = fragment,
	};
}

std::optional<URL> URL::create(std::string_view string) {
	auto n = string.find(":");
	if (n == std::string::npos) {
		// todo: default to https
		std::cerr << "Expected scheme in URL " << std::endl;
		return std::nullopt;
	}
	std::string scheme(string.substr(0, n));

	bool view_source = false;
	if (scheme == "view-source") {
		view_source = true;
		string = string.substr(n + 1);

		n = string.find(":");
		if (n == std::string::npos) {
			std::cerr << "Expected scheme after view-source in URL" << std::endl;
			return std::nullopt;
		}
		scheme = string.substr(0, n);
	}
	string = string.substr(n + 1);

	constexpr std::array supported_protocols{"http", "https", "file", "data", "about"};
	bool supported = std::find(supported_protocols.begin(), supported_protocols.end(), scheme) != supported_protocols.end();
	if (!supported) {
		std::cerr << "Unsupported protocol " << scheme << " in URL" << std::endl;
		return std::nullopt;
	}

	if (scheme == "data") {
		return parse_data_url(view_source, std::move(scheme), string);
	} else if (scheme == "about") {
		return parse_about_url(view_source, std::move(scheme), string);
	} else if (scheme == "file") {
		if (string.starts_with("//")) {
			return parse_standard_url(view_source, std::move(scheme), string.substr(2));
		} else {
			return parse_relative_file_url(view_source, std::move(scheme), string);
		}
	} else if (scheme == "http" || scheme == "https") {
		if (!string.starts_with("//")) {
			std::cerr << "Expected '//' after scheme: in " << scheme << " URL" << std::endl;
			return std::nullopt;
		}
		string = string.substr(2);
		return parse_standard_url(view_source, std::move(scheme), string);
	} else {
		assert(false && "unreachable");
	}
}

URL URL::ABOUT_BLANK = *URL::create("about:blank");

std::optional<URL> URL::resolve(std::string_view url_) const {
	if (url_.find("://") != std::string::npos || url_.find("file:.") != std::string::npos) {
		// url is abosolute or relative file
		return URL::create(url_);
	}
	std::string url { url_ };
	if (url.starts_with('#')) {
		URL out = *this;
		out.fragment = url.substr(1);
		return out;
	} else if (!url.starts_with('/')) {
		size_t dir_end = path.rfind("/");
		std::string_view dir { path.substr(0, dir_end) };

		while (url.starts_with("../")) {
			size_t new_url_start = url.find("/");
			assert(new_url_start != std::string::npos);
			url = url.substr(new_url_start);
			if (dir.find("/") != std::string::npos) {
				dir_end = path.rfind("/");
				dir = dir.substr(0, dir_end);
			}
		}

		url.insert(0, "/");
		url.insert(0, dir);
	}
	if (url.starts_with("//")) {
		return URL::create(scheme + "://" + url);
	} else {
		URL out = *this;
		size_t n = url.find('#');
		if (n != std::string::npos) {
			out.fragment = url.substr(n + 1);
			out.path = url.substr(0, n);
		} else {
			out.path = url;
		}
		return out;
	}
}

std::string URL::origin() const {
	// todo: relative file messes this up, but who cares about Same-Origin on files?
	return scheme + "://" + host + ":" + std::to_string(port);
}

bool URL::equal_disregarding_fragment(URL const& other) const {
	return view_source == other.view_source && scheme == other.scheme && host == other.host && port == other.port && path == other.path;
}

bool URL::operator==(const URL& other) const noexcept {
	return equal_disregarding_fragment(other) && fragment == other.fragment;
}

// todo: special hash and eq impls for this?
URL URL::reusable_connection_subsection() const {
	return URL {
		.view_source = false,
		.scheme = scheme,
		.host = host,
		.port = port,
		.path = "",
		.fragment = "",
	};
}

// todo: special hash and eq impls for this?
URL URL::cachable_subsection() const {
	return URL {
		.view_source = false,
		.scheme = scheme,
		.host = host,
		.port = port,
		.path = path,
		.fragment = "",
	};
}

std::ostream& operator<<(std::ostream& os, URL const& url) {
	char const *source = (url.view_source) ? "view-source:" : "";
	char const *scheme_delimeter = [&]() {
		if (url.scheme == "https" || url.scheme == "http" || (url.scheme == "file" && !url.path.starts_with('.'))) {
			return "://";
		} else {
			return ":";
		}
	}();
	auto port = [&]() -> std::optional<uint16_t> {
		if (url.scheme == "https" && url.port == 443) {
			return std::nullopt;
		} else if (url.scheme == "http" && url.port == 80) {
			return std::nullopt;
		} else if (url.scheme == "file" || url.scheme == "data" || url.scheme == "about") {
			return std::nullopt;
		} else {
			return url.port;
		}
	}();

	os << source << url.scheme << scheme_delimeter << url.host;
	if (port) {
		os << ":" << *port;
	}
	os << url.path;
	if (!url.fragment.empty()) {
		os << "#" << url.fragment;
	}
	return os;
}

std::size_t std::hash<URL>::operator()(const URL& u) const noexcept {
	size_t seed = 0;
	combine_hash(seed, std::hash<bool>{}(u.view_source));
	combine_hash(seed, std::hash<std::string>{}(u.scheme));
	combine_hash(seed, std::hash<std::string>{}(u.host));
	combine_hash(seed, std::hash<uint16_t>{}(u.port));
	combine_hash(seed, std::hash<std::string>{}(u.path));
	combine_hash(seed, std::hash<std::string>{}(u.fragment));
	return seed;
}
