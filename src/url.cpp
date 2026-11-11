#include "url.hpp"

#include <regex>

#include "util.hpp"

namespace mg {
namespace {

bool is_x_host(const std::string& h) {
  static const char* hosts[] = {"x.com", "twitter.com", "fxtwitter.com", "vxtwitter.com",
                                "fixupx.com", "fixvx.com", "nitter.net"};
  for (const char* base : hosts) {
    std::string b = base;
    if (h == b || ends_with(h, "." + b)) return true;
  }
  return false;
}

}  // namespace

std::string url_host(const std::string& url) {
  static const std::regex re(R"(^[a-zA-Z][a-zA-Z0-9+.-]*://(?:[^@/]*@)?([^/:?#]+))");
  std::smatch m;
  if (!std::regex_search(url, m, re)) return "";
  return to_lower(m[1].str());
}

std::optional<XStatusRef> parse_x_status(const std::string& url) {
  if (!is_x_host(url_host(url))) return std::nullopt;
  static const std::regex re(
      R"(^[a-zA-Z]+://[^/]+/(?:i/web/|(i|[A-Za-z0-9_]{1,15})/)(?:status|statuses|article)/(\d+))");
  std::smatch m;
  if (!std::regex_search(url, m, re)) return std::nullopt;
  std::string user = m[1].matched ? m[1].str() : "i";
  return XStatusRef{user, m[2].str()};
}

bool is_x_bare_article_link(const std::string& url) {
  if (!is_x_host(url_host(url))) return false;
  static const std::regex re(R"(^[a-zA-Z]+://[^/]+/i/article/\d+)");
  return std::regex_search(url, re);
}

}  // namespace mg
