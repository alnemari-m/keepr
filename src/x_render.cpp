// Renders X Articles (Draft.js "content_state": blocks + entityMap) to Markdown and HTML.
//
// Block types seen in the wild: unstyled, header-one/two/three, unordered-list-item,
// ordered-list-item, blockquote, code-block, atomic. Atomic blocks carry one entity:
// MEDIA (images/video/GIF), DIVIDER, MARKDOWN (code fences, tables), LATEX, TWEET.
// Inline entities: LINK. Inline styles: Bold, Italic, Strikethrough, Underline, Code.
//
// All input is untrusted: every field is read through type-tolerant helpers, every
// string is escaped for its output context, and link schemes are allow-listed.

#include "x_render.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

#include "json_util.hpp"
#include "util.hpp"

namespace mg {
using nlohmann::json;
using js::arr;
using js::num;
using js::obj;
using js::str;

namespace {

constexpr int kMaxListDepth = 8;

enum Style : unsigned { kBold = 1, kItalic = 2, kStrike = 4, kCode = 8, kUnderline = 16 };

unsigned style_bit(const std::string& name) {
  std::string s = to_lower(name);
  if (s == "bold") return kBold;
  if (s == "italic") return kItalic;
  if (s == "strikethrough") return kStrike;
  if (s == "code") return kCode;
  if (s == "underline") return kUnderline;
  return 0;
}

std::string ext_of(const std::string& url, const std::string& def) {
  std::string path = url.substr(0, url.find_first_of("?#"));
  auto slash = path.rfind('/');
  auto dot = path.rfind('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return def;
  std::string e = to_lower(path.substr(dot + 1));
  bool ok = !e.empty() && e.size() <= 5 &&
            std::all_of(e.begin(), e.end(), [](unsigned char c) { return std::isalnum(c); });
  return ok ? e : def;
}

// Entity keys appear both as numbers and as strings.
std::string key_of(const json& r) {
  if (!r.is_object()) return "";
  return str(r, "key");
}

// Allow-list link schemes: http, https, mailto. Bare hosts get https://.
// Anything else (javascript:, data:, vbscript:, file:, ...) is dropped -> "".
std::string safe_url(const std::string& raw) {
  std::string u = trim(raw);
  if (u.empty()) return "";
  auto colon = u.find(':');
  auto first_sep = u.find_first_of("/?#");
  bool has_scheme = colon != std::string::npos && (first_sep == std::string::npos || colon < first_sep);
  if (has_scheme) {
    std::string scheme = to_lower(u.substr(0, colon));
    bool valid = !scheme.empty() && std::isalpha(static_cast<unsigned char>(scheme[0])) &&
                 std::all_of(scheme.begin(), scheme.end(),
                             [](unsigned char c) { return std::isalnum(c) || c == '+' || c == '-' || c == '.'; });
    if (valid) {
      if (scheme == "http" || scheme == "https" || scheme == "mailto") return u;
      // "host:port/..." looks like a scheme but is a bare host with a port.
      bool port = colon + 1 < u.size() && std::isdigit(static_cast<unsigned char>(u[colon + 1]));
      if (!port) return "";
    }
  }
  return "https://" + u;
}

// Markdown link destination: percent-encode characters that would end or break it.
std::string md_url(const std::string& u) {
  std::string out;
  for (char c : u) {
    switch (c) {
      case ' ': out += "%20"; break;
      case '(': out += "%28"; break;
      case ')': out += "%29"; break;
      case '<': out += "%3C"; break;
      case '>': out += "%3E"; break;
      case '\n': case '\r': break;
      default: out.push_back(c);
    }
  }
  return out;
}

// Neutralise characters that start block syntax at the beginning of a Markdown line.
std::string md_line_start(const std::string& s) {
  std::size_t i = s.find_first_not_of(' ');
  if (i == std::string::npos) return s;
  char c = s[i];
  if (c == '#' || c == '>' || c == '-' || c == '+' || c == '|' || c == '=' || c == '~')
    return s.substr(0, i) + "\\" + s.substr(i);
  std::size_t j = i;
  while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
  if (j > i && j < s.size() && (s[j] == '.' || s[j] == ')')) return s.substr(0, j) + "\\" + s.substr(j);
  return s;
}

// A fence longer than any backtick run inside `code`.
std::string fence_for(const std::string& code) {
  std::size_t longest = 0, run = 0;
  for (char c : code) {
    run = c == '`' ? run + 1 : 0;
    longest = std::max(longest, run);
  }
  return std::string(std::max<std::size_t>(3, longest + 1), '`');
}

std::string md_inline_code(const std::string& code) {
  if (code.find('`') == std::string::npos) return "`" + code + "`";
  std::string f = fence_for(code).substr(2);  // fewer ticks than a block fence is fine inline
  if (f.size() < 2) f = "``";
  return f + " " + code + " " + f;
}

// Text safe to place inside an HTML comment.
std::string comment_safe(std::string s) {
  std::size_t p;
  while ((p = s.find("--")) != std::string::npos) s.replace(p, 2, "- -");
  return s;
}

struct Ctx {
  std::map<std::string, json> entities;  // entityMap key -> value
  std::map<std::string, json> media;     // media_id -> media_info
  const MediaResolver* resolve;
  std::vector<std::string>* warnings;
};

// ---------- inline text ----------

struct Inline {
  std::string md, html;
};

Inline render_inline(const json& block, const Ctx& ctx) {
  std::u16string text = utf8_to_u16(str(block, "text"));
  const std::size_t n = text.size();
  std::vector<unsigned> style(n, 0);
  std::vector<int> link(n, -1);
  std::vector<std::string> links;

  auto clamp_range = [n](long long off, long long len) {
    long long b = std::max<long long>(0, off);
    long long e = std::max<long long>(b, off + std::max<long long>(0, len));
    return std::pair{std::min<std::size_t>(static_cast<std::size_t>(b), n),
                     std::min<std::size_t>(static_cast<std::size_t>(std::min<long long>(e, static_cast<long long>(n))), n)};
  };

  for (const auto& r : arr(block, "inlineStyleRanges")) {
    unsigned bit = style_bit(str(r, "style"));
    auto [b, e] = clamp_range(num(r, "offset"), num(r, "length"));
    for (auto i = b; i < e; ++i) style[i] |= bit;
  }
  for (const auto& r : arr(block, "entityRanges")) {
    auto it = ctx.entities.find(key_of(r));
    if (it == ctx.entities.end() || str(it->second, "type") != "LINK") continue;
    std::string url = safe_url(str(obj(it->second, "data"), "url"));
    if (url.empty()) continue;
    links.push_back(url);
    auto [b, e] = clamp_range(num(r, "offset"), num(r, "length"));
    for (auto i = b; i < e; ++i) link[i] = static_cast<int>(links.size() - 1);
  }
  // Auto-detected mentions / bare URLs, unless an explicit LINK already covers them.
  const json& data = obj(block, "data");
  auto add_auto = [&](const char* field, bool mention) {
    for (const auto& r : arr(data, field)) {
      long long from = num(r, "fromIndex"), to = num(r, "toIndex");
      std::string t = str(r, "text");
      if (t.empty()) continue;
      std::string url;
      if (mention) {
        std::string h;
        for (char c : t) if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') h += c;
        if (h.empty()) continue;
        url = "https://x.com/" + h;
      } else {
        url = safe_url(t);
        if (url.empty()) continue;
      }
      auto [b, e] = clamp_range(from, to - from);
      bool free = true;
      for (auto i = b; i < e; ++i) free = free && link[i] < 0;
      if (!free) continue;
      links.push_back(url);
      for (auto i = b; i < e; ++i) link[i] = static_cast<int>(links.size() - 1);
    }
  };
  add_auto("mentions", true);
  add_auto("urls", false);

  Inline out;
  std::size_t i = 0;
  while (i < n) {
    std::size_t j = i;
    while (j < n && style[j] == style[i] && link[j] == link[i]) ++j;
    // don't split a surrogate pair between runs
    if (j < n && text[j] >= 0xDC00 && text[j] <= 0xDFFF) ++j;
    std::string run = u16_to_utf8(std::u16string_view(text).substr(i, j - i));
    unsigned s = style[i];
    int l = link[i];

    // Markdown markers must hug non-space text: move edge whitespace outside them.
    std::size_t lb = run.find_first_not_of(" \t");
    std::size_t le = run.find_last_not_of(" \t");
    std::string lead = lb == std::string::npos ? run : run.substr(0, lb);
    std::string core = lb == std::string::npos ? "" : run.substr(lb, le - lb + 1);
    std::string tail = lb == std::string::npos ? "" : run.substr(le + 1);

    std::string md = (s & kCode) ? md_inline_code(core) : md_escape(core);
    if (!core.empty()) {
      if (s & kItalic) md = "*" + md + "*";
      if (s & kBold) md = "**" + md + "**";
      if (s & kStrike) md = "~~" + md + "~~";
      if (l >= 0) md = "[" + md + "](" + md_url(links[l]) + ")";
    }
    out.md += lead + md + tail;

    std::string h = html_escape(run);
    if (s & kCode) h = "<code>" + h + "</code>";
    if (s & kItalic) h = "<em>" + h + "</em>";
    if (s & kBold) h = "<strong>" + h + "</strong>";
    if (s & kUnderline) h = "<u>" + h + "</u>";
    if (s & kStrike) h = "<s>" + h + "</s>";
    if (l >= 0) h = "<a href=\"" + html_escape(links[l]) + "\">" + h + "</a>";
    out.html += h;
    i = j;
  }
  // soft line breaks inside one block; each new line must not start block syntax
  std::string md2, html2;
  std::istringstream lines(out.md);
  bool first = true;
  for (std::string line; std::getline(lines, line); first = false) md2 += (first ? "" : "  \n") + md_line_start(line);
  for (char c : out.html) html2 += (c == '\n') ? std::string("<br>\n") : std::string(1, c);
  return {md2, html2};
}

// ---------- MARKDOWN entity (code fences, tables) -> HTML ----------

std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> lines;
  std::istringstream in(s);
  for (std::string l; std::getline(in, l);) lines.push_back(l);
  return lines;
}

std::vector<std::string> table_cells(std::string row) {
  row = trim(row);
  if (starts_with(row, "|")) row.erase(0, 1);
  if (ends_with(row, "|")) row.pop_back();
  std::vector<std::string> cells;
  std::string cur;
  for (std::size_t i = 0; i < row.size(); ++i) {
    if (row[i] == '\\' && i + 1 < row.size() && row[i + 1] == '|') { cur += '|'; ++i; continue; }
    if (row[i] == '|') { cells.push_back(trim(cur)); cur.clear(); continue; }
    cur += row[i];
  }
  cells.push_back(trim(cur));
  return cells;
}

std::string markdown_entity_to_html(const std::string& md) {
  auto lines = split_lines(md);
  std::string html;
  std::size_t i = 0;
  while (i < lines.size()) {
    std::string t = trim(lines[i]);
    if (starts_with(t, "```") || starts_with(t, "~~~")) {
      std::size_t flen = t.find_first_not_of(t[0]);
      if (flen == std::string::npos) flen = t.size();
      std::string fence = t.substr(0, flen), lang = trim(t.substr(flen));
      std::string code;
      for (++i; i < lines.size() && !starts_with(trim(lines[i]), fence); ++i) code += lines[i] + "\n";
      ++i;
      std::string cls;
      for (char c : lang) if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '#') cls += c;
      html += "<pre><code" + (cls.empty() ? "" : " class=\"language-" + cls + "\"") + ">" + html_escape(code) +
              "</code></pre>\n";
    } else if (starts_with(t, "|") && i + 1 < lines.size() && trim(lines[i + 1]).find("---") != std::string::npos) {
      html += "<div class=\"table\"><table>\n<thead><tr>";
      for (auto& c : table_cells(lines[i])) html += "<th>" + html_escape(c) + "</th>";
      html += "</tr></thead>\n<tbody>\n";
      for (i += 2; i < lines.size() && starts_with(trim(lines[i]), "|"); ++i) {
        html += "<tr>";
        for (auto& c : table_cells(lines[i])) html += "<td>" + html_escape(c) + "</td>";
        html += "</tr>\n";
      }
      html += "</tbody></table></div>\n";
    } else if (t.empty()) {
      ++i;
    } else {
      std::string para;
      for (; i < lines.size() && !trim(lines[i]).empty() && !starts_with(trim(lines[i]), "```"); ++i)
        para += (para.empty() ? "" : "\n") + lines[i];
      html += "<pre class=\"md\">" + html_escape(para) + "</pre>\n";
    }
  }
  return html;
}

// ---------- atomic blocks ----------

struct Out {
  std::string md, html;
};

void render_media_entity(const json& data, const Ctx& ctx, Out& out) {
  std::string caption = str(data, "caption");
  std::string figure;
  std::string md;
  for (const auto& item : arr(data, "mediaItems")) {
    std::string id = str(item, "mediaId");
    if (id.empty()) id = str(item, "media_id");
    auto it = ctx.media.find(id);
    if (it == ctx.media.end()) {
      ctx.warnings->push_back("article media " + id + " not found in media_entities");
      continue;
    }
    const json& info = it->second;
    std::string type = str(info, "__typename");
    std::string img = str(info, "original_img_url");
    if (!img.empty() && (type == "ApiImage" || type.empty())) {
      std::string href = (*ctx.resolve)(image_ref(img, "body"));
      std::string alt = caption.empty() ? "image" : caption;
      md += "![" + md_escape(alt) + "](" + md_url(href) + ")\n";
      figure += "<img src=\"" + html_escape(href) + "\" alt=\"" + html_escape(alt) + "\" loading=\"lazy\">\n";
    } else if (auto v = video_ref(info, "body")) {
      std::string href = (*ctx.resolve)(*v);
      md += "[▶ " + v->kind + "](" + md_url(href) + ")\n";
      figure += "<video controls preload=\"metadata\"" + std::string(v->kind == "gif" ? " autoplay loop muted" : "") +
                " src=\"" + html_escape(href) + "\"></video>\n";
    } else if (!img.empty()) {  // video with only a poster image
      std::string href = (*ctx.resolve)(image_ref(img, "body"));
      md += "![video poster](" + md_url(href) + ")\n";
      figure += "<img src=\"" + html_escape(href) + "\" alt=\"video poster\">\n";
      ctx.warnings->push_back("no downloadable mp4 for media " + id + " (" + type + "); saved poster only");
    } else {
      ctx.warnings->push_back("unsupported media type " + type + " (" + id + ")");
    }
  }
  if (!caption.empty()) {
    md += "\n*" + md_escape(caption) + "*\n";
    figure += "<figcaption>" + html_escape(caption) + "</figcaption>\n";
  }
  out.md += md + "\n";
  out.html += "<figure>\n" + figure + "</figure>\n";
}

void render_atomic(const json& block, const Ctx& ctx, Out& out) {
  for (const auto& r : arr(block, "entityRanges")) {
    auto it = ctx.entities.find(key_of(r));
    if (it == ctx.entities.end()) continue;
    const json& ent = it->second;
    const std::string type = str(ent, "type");
    const json& data = obj(ent, "data");

    if (type == "MEDIA") {
      render_media_entity(data, ctx, out);
    } else if (type == "DIVIDER") {
      out.md += "---\n\n";
      out.html += "<hr>\n";
    } else if (type == "MARKDOWN") {
      std::string md = str(data, "markdown");
      out.md += md + (ends_with(md, "\n") ? "\n" : "\n\n");
      out.html += markdown_entity_to_html(md);
    } else if (type == "LATEX") {
      std::string tex = str(data, "latex");
      if (tex.empty()) tex = str(data, "markdown");
      if (tex.empty()) tex = str(block, "text");
      out.md += "$$\n" + trim(tex) + "\n$$\n\n";
      out.html += "<div class=\"math\">\\[" + html_escape(trim(tex)) + "\\]</div>\n";
    } else if (type == "TWEET" || type == "POST") {
      std::string id = str(data, "tweetId");
      if (id.empty()) id = str(data, "postId");
      bool numeric = !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isdigit(c); });
      if (!numeric) {
        ctx.warnings->push_back("embedded post with invalid id skipped");
        continue;
      }
      std::string url = "https://x.com/i/status/" + id;
      out.md += "> Embedded post: <" + url + ">\n\n";
      out.html += "<blockquote class=\"embed\">Embedded post: <a href=\"" + url + "\">" + url + "</a></blockquote>\n";
    } else if (type == "LINK") {
      std::string url = safe_url(str(data, "url"));
      if (url.empty()) continue;
      out.md += "<" + md_url(url) + ">\n\n";
      out.html += "<p><a href=\"" + html_escape(url) + "\">" + html_escape(url) + "</a></p>\n";
    } else {
      ctx.warnings->push_back("unsupported atomic entity type '" + type + "' (raw data kept in the .json file)");
      out.md += "<!-- unsupported entity " + comment_safe(type + ": " + data.dump()) + " -->\n\n";
      out.html += "<pre class=\"unsupported\">" + html_escape(type + " " + data.dump(2)) + "</pre>\n";
    }
  }
}

// ---------- document assembly ----------

const char* kCss = R"CSS(
:root{--bg:#fff;--fg:#0f1419;--muted:#536471;--border:#eff3f4;--code-bg:#f6f8fa;--link:#1d9bf0}
@media (prefers-color-scheme:dark){:root{--bg:#000;--fg:#e7e9ea;--muted:#71767b;--border:#2f3336;--code-bg:#16181c;--link:#1d9bf0}}
*{box-sizing:border-box}
body{background:var(--bg);color:var(--fg);margin:0;font:17px/1.65 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"Noto Sans","Noto Sans Arabic",Helvetica,Arial,sans-serif}
main{max-width:680px;margin:0 auto;padding:32px 16px 96px}
h1{font-size:2.1em;line-height:1.2;margin:.6em 0 .3em}h2{font-size:1.5em;margin:1.6em 0 .4em}h3{font-size:1.2em;margin:1.4em 0 .3em}
.byline{color:var(--muted);font-size:.9em;margin-bottom:1.5em}.byline a{color:var(--muted)}
a{color:var(--link)}img,video{max-width:100%;height:auto;border-radius:12px;display:block;margin:0 auto}
.cover{width:100%;margin-bottom:8px}figure{margin:1.6em 0}figcaption{color:var(--muted);font-size:.85em;margin-top:.4em}
pre{background:var(--code-bg);border:1px solid var(--border);border-radius:8px;padding:14px 16px;overflow-x:auto;font-size:.82em;line-height:1.5}
code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}p code{background:var(--code-bg);padding:.1em .3em;border-radius:4px}
blockquote{border-left:4px solid var(--border);margin:1em 0;padding:.1em 1em;color:var(--muted)}
hr{border:0;border-top:1px solid var(--border);margin:2em 0}
.table{overflow-x:auto}table{border-collapse:collapse;font-size:.9em}th,td{border:1px solid var(--border);padding:6px 10px;text-align:start}
.math{overflow-x:auto;text-align:center;margin:1em 0}
footer{color:var(--muted);font-size:.8em;border-top:1px solid var(--border);margin-top:3em;padding-top:1em}
)CSS";

std::string author_line(const json& tweet) {
  const json& a = obj(tweet, "author");
  std::string name = str(a, "name"), handle = str(a, "screen_name");
  return name + (handle.empty() ? "" : " (@" + handle + ")");
}

std::string safe_lang(const std::string& l) {
  std::string out;
  for (char c : l) if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') out += c;
  return out.empty() || out == "zxx" || out == "und" ? "en" : out;
}

std::string html_document(const std::string& title, const std::string& lang, const std::string& head_extra,
                          const std::string& body) {
  return "<!doctype html>\n<html lang=\"" + safe_lang(lang) + "\" dir=\"auto\">\n<head>\n<meta charset=\"utf-8\">\n"
         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>" +
         html_escape(title) + "</title>\n<style>" + kCss + "</style>\n" + head_extra + "</head>\n<body>\n<main dir=\"auto\">\n" +
         body + "</main>\n</body>\n</html>\n";
}

// First `max_units` UTF-16 units of `s` on one line, never splitting a character.
std::string short_title(const std::string& s, std::size_t max_units) {
  std::string flat;
  bool space = false;
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') { space = true; continue; }
    if (space && !flat.empty()) flat += ' ';
    space = false;
    flat += c;
  }
  std::u16string u = utf8_to_u16(flat);
  if (u.size() <= max_units) return flat;
  std::size_t cut = max_units;
  if (cut > 0 && u[cut - 1] >= 0xD800 && u[cut - 1] <= 0xDBFF) --cut;
  return u16_to_utf8(std::u16string_view(u).substr(0, cut)) + "…";
}

}  // namespace

MediaRef image_ref(const std::string& url, const std::string& role) {
  std::string base = url.substr(0, url.find('?'));
  std::string ext = ext_of(base, "jpg");
  // pbs.twimg.com serves the original upload with ?format=<ext>&name=orig
  if (starts_with(url, "https://pbs.twimg.com/") && ends_with(base, "." + ext)) {
    std::string stem = base.substr(0, base.size() - ext.size() - 1);
    return {stem + "?format=" + ext + "&name=orig", "image", role, ext};
  }
  return {url, "image", role, ext};
}

std::optional<MediaRef> video_ref(const json& info, const std::string& role) {
  std::string best;
  long long best_rate = -1;
  std::function<void(const json&, int)> walk = [&](const json& j, int depth) {
    if (depth > 16) return;
    if (j.is_object()) {
      for (const auto& v : arr(j, "variants")) {
        std::string ct = str(v, "content_type");
        if (ct.empty()) ct = str(v, "contentType");
        std::string u = str(v, "url");
        long long rate = num(v, "bit_rate", num(v, "bitrate", 0));
        if (ct == "video/mp4" && starts_with(u, "https://") && rate > best_rate) { best = u; best_rate = rate; }
      }
      for (const auto& [k, v] : j.items()) walk(v, depth + 1);
    } else if (j.is_array()) {
      for (const auto& v : j) walk(v, depth + 1);
    }
  };
  walk(info, 0);
  if (best.empty()) return std::nullopt;
  std::string type = to_lower(str(info, "__typename"));
  return MediaRef{best, type.find("gif") != std::string::npos ? "gif" : "video", role, "mp4"};
}

RenderedDoc render_article(const json& tweet, const MediaResolver& resolve) {
  RenderedDoc doc;
  const json& article = obj(tweet, "article");
  const json& content = article.contains("content") ? obj(article, "content") : obj(article, "content_state");

  Ctx ctx{{}, {}, &resolve, &doc.warnings};
  if (content.contains("entityMap")) {
    const json& em = content["entityMap"];
    if (em.is_array()) {
      for (const auto& e : em) ctx.entities[str(e, "key")] = obj(e, "value");
    } else if (em.is_object()) {
      for (const auto& [k, v] : em.items()) ctx.entities[k] = v.is_object() ? v : json::object();
    }
  }
  for (const auto& m : arr(article, "media_entities")) ctx.media[str(m, "media_id")] = obj(m, "media_info");

  doc.title = str(article, "title");
  if (doc.title.empty()) doc.title = "Untitled article";
  const std::string source = safe_url(str(tweet, "url"));
  const std::string published = str(article, "created_at", str(tweet, "created_at"));
  const std::string author = author_line(tweet);

  std::string cover_md, cover_html;
  std::string cover = str(obj(obj(article, "cover_media"), "media_info"), "original_img_url");
  if (!cover.empty()) {
    std::string href = resolve(image_ref(cover, "cover"));
    cover_md = "![cover](" + md_url(href) + ")\n\n";
    cover_html = "<img class=\"cover\" src=\"" + html_escape(href) + "\" alt=\"cover\">\n";
  }

  Out body;
  struct Level {
    std::string tag;   // "ul" / "ol"
    bool li_open;
  };
  std::vector<Level> lists;  // html list stack; nested lists live inside the parent's open <li>
  std::vector<int> counters; // md ordered-list counters per depth
  bool in_quote_md = false;
  auto close_lists = [&](std::size_t keep) {
    while (lists.size() > keep) {
      if (lists.back().li_open) body.html += "</li>\n";
      body.html += "</" + lists.back().tag + ">\n";
      lists.pop_back();
    }
  };

  for (const auto& block : arr(content, "blocks")) {
    const std::string type = str(block, "type");
    const int depth = static_cast<int>(std::clamp<long long>(num(block, "depth"), 0, kMaxListDepth));
    const bool is_list = ends_with(type, "list-item");

    if (!is_list) {
      if (!lists.empty()) body.md += "\n";
      close_lists(0);
      counters.clear();
    }
    if (type != "blockquote" && in_quote_md) { body.md += "\n"; in_quote_md = false; }

    if (is_list) {
      const std::string tag = starts_with(type, "ordered") ? "ol" : "ul";
      const std::size_t want = static_cast<std::size_t>(depth) + 1;
      close_lists(want);
      if (lists.size() == want && lists.back().tag != tag) close_lists(want - 1);
      if (lists.size() == want && lists.back().li_open) {
        body.html += "</li>\n";
        lists.back().li_open = false;
      }
      while (lists.size() < want) {
        lists.push_back({tag, false});
        body.html += "<" + tag + ">\n";
      }
      counters.resize(want, 0);
      if (tag == "ol") ++counters[want - 1]; else counters[want - 1] = 0;
      Inline t = render_inline(block, ctx);
      body.md += std::string(depth * 3, ' ') + (tag == "ol" ? std::to_string(counters[want - 1]) + ". " : "- ") + t.md + "\n";
      body.html += "<li>" + t.html;
      lists.back().li_open = true;
      continue;
    }

    if (type == "atomic") {
      render_atomic(block, ctx, body);
    } else if (type == "header-one" || type == "header-two" || type == "header-three") {
      int lvl = type == "header-three" ? 3 : 2;  // the article title owns the single H1
      Inline t = render_inline(block, ctx);
      body.md += std::string(lvl, '#') + " " + t.md + "\n\n";
      std::string h = "h" + std::to_string(lvl);
      body.html += "<" + h + ">" + t.html + "</" + h + ">\n";
    } else if (type == "blockquote") {
      Inline t = render_inline(block, ctx);
      body.md += (in_quote_md ? ">\n> " : "> ") + t.md + "\n";
      in_quote_md = true;
      body.html += "<blockquote>" + t.html + "</blockquote>\n";
    } else if (type == "code-block") {
      std::string code = str(block, "text");
      std::string f = fence_for(code);
      body.md += f + "\n" + code + "\n" + f + "\n\n";
      body.html += "<pre><code>" + html_escape(code) + "</code></pre>\n";
    } else {
      if (type != "unstyled") doc.warnings.push_back("unknown block type '" + type + "' rendered as paragraph");
      Inline t = render_inline(block, ctx);
      if (trim(t.md).empty()) continue;
      body.md += t.md + "\n\n";
      body.html += "<p>" + t.html + "</p>\n";
    }
  }
  close_lists(0);

  const std::string captured = now_iso8601();
  doc.markdown = "---\ntitle: " + yaml_quote(doc.title) + "\nauthor: " + yaml_quote(author) +
                 "\nsource: " + yaml_quote(source) + "\npublished: " + yaml_quote(published) + "\ncaptured: " + captured +
                 "\n---\n\n# " + md_escape(short_title(doc.title, 1000)) + "\n\n" + cover_md + "*" + md_escape(author) +
                 " · " + md_escape(published) + " · [source](" + md_url(source) + ")*\n\n" + body.md;

  bool has_math = body.html.find("class=\"math\"") != std::string::npos;
  std::string head_extra =
      has_math ? "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.css\">\n"
                 "<script defer src=\"https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js\"></script>\n"
                 "<script defer src=\"https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/contrib/auto-render.min.js\" "
                 "onload=\"renderMathInElement(document.body)\"></script>\n"
               : "";
  std::string html_body = cover_html + "<h1>" + html_escape(doc.title) + "</h1>\n<div class=\"byline\">" +
                          html_escape(author) + " · " + html_escape(published) + " · <a href=\"" + html_escape(source) +
                          "\">source</a></div>\n" + body.html + "<footer>Saved by Keepr on " + captured +
                          " from <a href=\"" + html_escape(source) + "\">" + html_escape(source) + "</a></footer>\n";
  doc.html = html_document(short_title(doc.title, 200), str(tweet, "lang", "en"), head_extra, html_body);
  return doc;
}

RenderedDoc render_post(const json& tweet, const MediaResolver& resolve) {
  RenderedDoc doc;
  const std::string source = safe_url(str(tweet, "url"));
  const std::string author = author_line(tweet);
  const std::string text = str(tweet, "text");
  doc.title = author + ": " + short_title(text, 60);

  std::string md_media, html_media;
  const json& media = obj(tweet, "media");
  for (const auto& p : arr(media, "photos")) {
    std::string url = str(p, "url");
    if (url.empty()) continue;
    std::string href = resolve(image_ref(url, "post"));
    md_media += "![photo](" + md_url(href) + ")\n\n";
    html_media += "<figure><img src=\"" + html_escape(href) + "\" alt=\"photo\"></figure>\n";
  }
  for (const auto& v : arr(media, "videos")) {
    std::string url = str(v, "url");
    if (url.empty()) continue;
    std::string vt = str(v, "type");
    std::string kind = vt == "gif" ? "gif" : vt == "audio" ? "audio" : "video";
    std::string href = resolve(MediaRef{url, kind, "post", "mp4"});
    md_media += "[▶ " + kind + "](" + md_url(href) + ")\n\n";
    html_media += kind == "audio"
                      ? "<figure><audio controls src=\"" + html_escape(href) + "\"></audio></figure>\n"
                      : "<figure><video controls preload=\"metadata\" src=\"" + html_escape(href) + "\"></video></figure>\n";
  }

  std::string body_md, body_html;
  for (auto& line : split_lines(text)) {
    body_md += md_line_start(md_escape(line)) + "  \n";
    body_html += html_escape(line) + "<br>\n";
  }
  const std::string date = str(tweet, "created_at");
  doc.markdown = "---\nauthor: " + yaml_quote(author) + "\nsource: " + yaml_quote(source) + "\npublished: " +
                 yaml_quote(date) + "\ncaptured: " + now_iso8601() + "\n---\n\n**" + md_escape(author) + "** · " +
                 md_escape(date) + "\n\n" + body_md + "\n" + md_media + "[source](" + md_url(source) + ")\n";
  const json& quote = obj(tweet, "quote");
  if (!quote.empty())
    doc.markdown += "\n> Quoting " + md_escape(author_line(quote)) + ": " +
                    md_escape(short_title(str(quote, "text"), 280)) + "\n> <" + md_url(safe_url(str(quote, "url"))) + ">\n";
  doc.html = html_document(doc.title, str(tweet, "lang", "en"), "",
                           "<div class=\"byline\">" + html_escape(author) + " · " + html_escape(date) + "</div>\n<p>" +
                               body_html + "</p>\n" + html_media + "<footer><a href=\"" + html_escape(source) +
                               "\">source</a></footer>\n");
  return doc;
}

}  // namespace mg
