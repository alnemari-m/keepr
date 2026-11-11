#pragma once
#include <optional>
#include <string>

namespace mg {

struct XStatusRef {
  std::string user;  // may be "i" when the URL has no handle
  std::string id;    // numeric status (post) id
};

// Recognises x.com / twitter.com / fxtwitter / vxtwitter / fixupx links of the form
//   /<user>/status/<id>, /i/status/<id>, /i/web/status/<id>, /<user>/article/<id>
std::optional<XStatusRef> parse_x_status(const std::string& url);

// True for x.com/i/article/<id> links, which carry the article id, not the post id.
bool is_x_bare_article_link(const std::string& url);

std::string url_host(const std::string& url);

}  // namespace mg
