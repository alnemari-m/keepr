#pragma once
#include <string>
#include <string_view>

namespace mg {

// Draft.js (X Articles) offsets are JavaScript string indices, i.e. UTF-16 code units.
std::u16string utf8_to_u16(std::string_view s);
std::string u16_to_utf8(std::u16string_view s);

std::string html_escape(std::string_view s);
std::string md_escape(std::string_view s);
std::string yaml_quote(std::string_view s);

// Filesystem-safe name. Keeps non-ASCII letters (Arabic, CJK, ...) intact.
std::string slugify(std::string_view s, std::size_t max_bytes = 80);

std::string trim(std::string_view s);
std::string to_lower(std::string_view s);
bool starts_with(std::string_view s, std::string_view p);
bool ends_with(std::string_view s, std::string_view p);
std::string now_iso8601();

}  // namespace mg
