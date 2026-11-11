// Minimal self-contained test runner (no framework dependency).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "url.hpp"
#include "util.hpp"
#include "video.hpp"
#include "x_render.hpp"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    if (cond) { ++g_pass; }                                                      \
    else { ++g_fail; std::cerr << __FILE__ << ":" << __LINE__ << ": FAIL " #cond "\n"; } \
  } while (0)
static bool has(const std::string& hay, const std::string& needle) {
  bool ok = hay.find(needle) != std::string::npos;
  if (!ok) std::cerr << "  missing: " << needle << "\n";
  return ok;
}

static void test_utf() {
  std::string s = "a😀b مرحبا";
  auto u = mg::utf8_to_u16(s);
  CHECK(u.size() == 10);  // a, surrogate pair (2), b, space, 5 Arabic letters
  CHECK(mg::u16_to_utf8(u) == s);
  CHECK(mg::slugify("Hello, World! 2026") == "hello-world-2026");
  CHECK(mg::slugify("مرحبا بالعالم") == "مرحبا-بالعالم");
  CHECK(mg::slugify("???").empty());
  std::string long_ar(200, 'x');
  CHECK(mg::slugify(long_ar, 10).size() == 10);
  CHECK(mg::html_escape("<a href=\"x\">&") == "&lt;a href=&quot;x&quot;&gt;&amp;");
}

static void test_url() {
  auto r = mg::parse_x_status("https://x.com/karpathy/status/2002118205729562949?s=20");
  CHECK(r && r->user == "karpathy" && r->id == "2002118205729562949");
  r = mg::parse_x_status("https://twitter.com/i/web/status/123");
  CHECK(r && r->user == "i" && r->id == "123");
  r = mg::parse_x_status("https://mobile.twitter.com/a_b/status/42/photo/1");
  CHECK(r && r->user == "a_b" && r->id == "42");
  r = mg::parse_x_status("https://x.com/karpathy/article/2002118205729562949");
  CHECK(r && r->id == "2002118205729562949");
  CHECK(!mg::parse_x_status("https://www.tiktok.com/@user/video/7300000000000000000"));
  CHECK(!mg::parse_x_status("https://evil.com/x.com/a/status/1"));
  CHECK(!mg::parse_x_status("https://notx.com/a/status/1"));
  CHECK(mg::is_x_bare_article_link("https://x.com/i/article/2055126803224883200"));
  CHECK(mg::url_host("https://user@www.TikTok.com:443/x") == "www.tiktok.com");
}

static void test_ytdlp_argv() {
  mg::VideoOptions o;
  o.max_height = 720;
  auto a = mg::ytdlp_argv("-rf", "out/%(id)s.%(ext)s", o);
  CHECK(a.size() >= 2 && a[a.size() - 2] == "--" && a.back() == "-rf");  // URL can't become a flag
  bool cap = false;
  for (auto& s : a) cap = cap || s.find("height<=720") != std::string::npos;
  CHECK(cap);
  o = {};
  o.audio_only = true;
  a = mg::ytdlp_argv("u", "t", o);
  bool mp3 = false;
  for (auto& s : a) mp3 = mp3 || s == "mp3";
  CHECK(mp3);
}

static void test_image_ref() {
  auto m = mg::image_ref("https://pbs.twimg.com/media/HIt1uk1aEAABIiK.jpg", "body");
  CHECK(m.url == "https://pbs.twimg.com/media/HIt1uk1aEAABIiK?format=jpg&name=orig");
  CHECK(m.ext == "jpg");
  m = mg::image_ref("https://pbs.twimg.com/media/X.png?name=small", "body");
  CHECK(m.url == "https://pbs.twimg.com/media/X?format=png&name=orig");
}

static void test_article() {
  std::ifstream f(std::string(FIXTURE_DIR) + "/article.json");
  auto j = nlohmann::json::parse(f);
  std::vector<mg::MediaRef> seen;
  auto resolver = [&](const mg::MediaRef& m) {
    seen.push_back(m);
    return "assets/" + m.kind + std::to_string(seen.size()) + "." + m.ext;
  };
  auto doc = mg::render_article(j["tweet"], resolver);
  const auto& md = doc.markdown;
  const auto& html = doc.html;

  CHECK(doc.title == "Hello *World*: a test");
  CHECK(has(md, "# Hello \\*World\\*: a test"));
  CHECK(has(md, "😀 **bold** [link](https://example.com) here"));  // UTF-16 offsets after emoji
  CHECK(has(html, "😀 <strong>bold</strong> <a href=\"https://example.com\">link</a> here"));
  CHECK(has(md, "## Section"));
  CHECK(has(md, "- one\n   - nested\n"));
  CHECK(has(md, "1. first\n2. second\n"));
  CHECK(has(html, "<ol>\n<li>first</li>\n<li>second</li>\n</ol>"));
  CHECK(has(md, "![A caption](assets/image2.png)"));
  CHECK(has(html, "<figcaption>A caption</figcaption>"));
  CHECK(has(md, "```cpp\nint main() { return a < b; }\n```"));
  CHECK(has(html, "<pre><code class=\"language-cpp\">int main() { return a &lt; b; }\n</code></pre>"));
  CHECK(has(md, "---\n"));
  CHECK(has(html, "<hr>"));
  CHECK(has(md, "$$\nE = mc^2\n$$"));
  CHECK(has(html, "katex"));
  CHECK(has(html, "<th>A</th><th>B</th>"));
  CHECK(has(html, "<td>1</td><td>2</td>"));
  CHECK(has(html, "<video controls"));
  CHECK(has(md, "> quoted"));
  CHECK(has(md, "[@someone](https://x.com/someone)"));
  CHECK(has(md, "مرحبا *بالعالم*"));
  CHECK(has(md, "![cover](assets/image1.jpg)"));

  // media resolution: cover, body image (orig quality), highest-bitrate mp4
  CHECK(seen.size() == 3);
  if (seen.size() == 3) {
    CHECK(seen[0].role == "cover");
    CHECK(seen[1].url == "https://pbs.twimg.com/media/IMG1?format=png&name=orig");
    CHECK(seen[2].url == "https://video.twimg.com/high.mp4" && seen[2].kind == "video");
  }
  CHECK(doc.warnings.empty());
  for (auto& w : doc.warnings) std::cerr << "  warning: " << w << "\n";
}


static void test_hostile() {
  using nlohmann::json;
  auto noop = [](const mg::MediaRef& m) { return m.url; };
  json t = json::parse(R"J({
    "url": "https://x.com/a/status/1", "author": null, "lang": "\"><x",
    "article": {"title": null, "cover_media": null, "media_entities": null,
      "content": {"entityMap": [
        {"key": "0", "value": {"type": "LINK", "data": {"url": "javascript:alert(1)//x://y"}}},
        {"key": "1", "value": {"type": "TWEET", "data": {"tweetId": "1\"><script>x</script>"}}},
        {"key": "2", "value": {"type": "WEIRD", "data": {"v": "--> <script>bad()</script>"}}},
        {"key": "3", "value": {"type": "LINK", "data": {"url": "example.com/a b(c)"}}},
        {"key": "4", "value": null}
      ],
      "blocks": [
        {"type": "unstyled", "text": "click me", "inlineStyleRanges": [{"offset": null, "length": "x", "style": null}],
         "entityRanges": [{"key": 0, "offset": 0, "length": 5}, {"offset": 0}, 7, {"key": 4, "offset": 0, "length": 1}], "data": null},
        {"type": "atomic", "text": " ", "entityRanges": [{"key": 1, "offset": 0, "length": 1}]},
        {"type": "atomic", "text": " ", "entityRanges": [{"key": 2, "offset": 0, "length": 1}]},
        {"type": "unstyled", "text": "go here", "entityRanges": [{"key": 3, "offset": 0, "length": 7}]},
        {"type": "unordered-list-item", "text": "deep", "depth": 1000000000},
        {"type": "unordered-list-item", "text": "neg", "depth": -5},
        {"type": "code-block", "text": "a ``` b"},
        {"type": "unstyled", "text": "# not a heading\n- not a list"},
        {"type": "unstyled", "text": "x", "inlineStyleRanges": [{"offset": -10, "length": 99999999999, "style": "Bold"}]}
      ]}}})J");
  mg::RenderedDoc d;
  bool threw = false;
  try { d = mg::render_article(t, noop); } catch (const std::exception& e) { threw = true; std::cerr << e.what() << "\n"; }
  CHECK(!threw);
  CHECK(d.html.find("javascript:") == std::string::npos);
  CHECK(d.markdown.find("javascript:") == std::string::npos);
  CHECK(d.html.find("<script>x") == std::string::npos);
  CHECK(d.html.find("<script>bad") == std::string::npos);
  CHECK(d.markdown.find("--> <script>") == std::string::npos);
  CHECK(has(d.markdown, "[go here](https://example.com/a%20b%28c%29)"));
  CHECK(d.html.size() < 100000);  // depth clamped
  CHECK(has(d.markdown, "````\na ``` b\n````"));
  CHECK(has(d.markdown, "\\# not a heading  \n\\- not a list"));
  CHECK(has(d.html, "<html lang=\"x\""));
  CHECK(has(d.markdown, "**x**"));

  json post = json::parse(R"J({"url": "https://x.com/a/status/2", "media": "oops", "quote": null,
    "author": {"name": "N", "screen_name": "n"},
    "text": "مرحبا مرحبا مرحبا مرحبا مرحبا مرحبا مرحبا مرحبا مرحبا مرحبا مرحبا مرحبا\nline2"})J");
  threw = false;
  try { d = mg::render_post(post, noop); } catch (const std::exception& e) { threw = true; std::cerr << e.what() << "\n"; }
  CHECK(!threw);
  CHECK(mg::u16_to_utf8(mg::utf8_to_u16(d.title)) == d.title);  // valid UTF-8, no lone bytes
  CHECK(d.title.find('\n') == std::string::npos);
  CHECK(has(d.markdown, "  \nline2"));

  CHECK(mg::ytdlp_escape_path("/tmp/100%/x") == "/tmp/100%%/x");
}


int main() {
  test_utf();
  test_url();
  test_ytdlp_argv();
  test_image_ref();
  test_article();
  test_hostile();
  std::cout << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail ? 1 : 0;
}
