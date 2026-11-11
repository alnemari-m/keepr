#include "util.hpp"

#include <cctype>
#include <ctime>

namespace mg {

std::u16string utf8_to_u16(std::string_view s) {
  std::u16string out;
  out.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    char32_t cp = 0xFFFD;
    std::size_t len = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c >> 5) == 0x6) {
      len = 2;
    } else if ((c >> 4) == 0xE) {
      len = 3;
    } else if ((c >> 3) == 0x1E) {
      len = 4;
    }
    if (len > 1) {
      if (i + len > s.size()) {
        len = 1;
      } else {
        cp = c & (0xFF >> (len + 1));
        for (std::size_t k = 1; k < len; ++k) {
          unsigned char cc = static_cast<unsigned char>(s[i + k]);
          if ((cc >> 6) != 0x2) { cp = 0xFFFD; len = 1; break; }
          cp = (cp << 6) | (cc & 0x3F);
        }
      }
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
    i += len;
  }
  return out;
}

std::string u16_to_utf8(std::u16string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    char32_t cp = s[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
      ++i;
    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
      cp = 0xFFFD;  // lone surrogate (e.g. a range that split a pair)
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

std::string html_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

std::string md_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': case '`': case '*': case '_': case '[': case ']': case '<':
        out.push_back('\\');
        [[fallthrough]];
      default:
        out.push_back(c);
    }
  }
  return out;
}

std::string yaml_quote(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    if (c == '\n') { out += "\\n"; continue; }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string slugify(std::string_view s, std::size_t max_bytes) {
  std::string out;
  bool dash = false;
  for (char ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c >= 0x80 || std::isalnum(c)) {
      if (dash && !out.empty()) out.push_back('-');
      dash = false;
      out.push_back(c < 0x80 ? static_cast<char>(std::tolower(c)) : ch);
    } else {
      dash = true;
    }
  }
  if (out.size() > max_bytes) {
    std::size_t cut = max_bytes;
    // never cut inside a UTF-8 sequence
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
    out.resize(cut);
    while (!out.empty() && out.back() == '-') out.pop_back();
  }
  return out;
}

std::string trim(std::string_view s) {
  std::size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

std::string to_lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool ends_with(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

std::string now_iso8601() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

}  // namespace mg
