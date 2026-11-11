#include "x_download.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "http.hpp"
#include "json_util.hpp"
#include "util.hpp"
#include "x_render.hpp"

namespace mg {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

void write_file(const fs::path& p, const std::string& data) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write " + p.string());
  f << data;
}

json fetch_status(Http& http, const XOptions& opt, const XStatusRef& ref) {
  // fxtwitter accepts any handle in the path; it resolves by id.
  std::string url = opt.api_base + "/" + (ref.user.empty() ? "i" : ref.user) + "/status/" + ref.id;
  HttpResponse r = http.get(url);
  json j = json::parse(r.body, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded()) throw std::runtime_error("X: unexpected response (HTTP " + std::to_string(r.status) + ") from " + url);
  if (r.status != 200 || js::num(j, "code") != 200 || js::obj(j, "tweet").empty()) {
    std::string msg = js::str(j, "message", "unknown error");
    throw std::runtime_error("X: post " + ref.id + " unavailable (HTTP " + std::to_string(r.status) + ": " + msg +
                             "). Private, deleted or age-restricted posts cannot be fetched.");
  }
  return j;
}

// Downloads each distinct remote URL once and hands back a path relative to `dir`.
class AssetStore {
 public:
  AssetStore(Http& http, fs::path dir, bool enabled) : http_(http), dir_(std::move(dir)), enabled_(enabled) {}

  std::string operator()(const MediaRef& m) {
    if (!enabled_ || m.url.empty() || !starts_with(m.url, "http")) return m.url;
    if (auto it = done_.find(m.url); it != done_.end()) return it->second;

    std::string name;
    if (m.role == "cover") {
      name = "cover." + m.ext;
    } else {
      int& n = counters_[m.kind];
      char buf[64];
      std::snprintf(buf, sizeof buf, "%s-%02d.%s", m.kind == "image" ? "img" : m.kind.c_str(), ++n, m.ext.c_str());
      name = buf;
    }
    fs::path rel = fs::path("assets") / name;
    if (m.role == "post") rel = name;  // plain posts keep media next to post.md
    fs::path dest = dir_ / rel;
    try {
      if (!fs::exists(dest)) {
        std::cerr << "  ↓ " << rel.string() << "\n";
        http_.download(m.url, dest);
      }
      done_[m.url] = rel.generic_string();
    } catch (const std::exception& e) {
      // Fall back to a remote link so the document is still complete.
      std::cerr << "  ! " << e.what() << " (keeping remote link)\n";
      done_[m.url] = m.url;
    }
    return done_[m.url];
  }

  std::size_t count() const { return done_.size(); }

 private:
  Http& http_;
  fs::path dir_;
  bool enabled_;
  std::map<std::string, std::string> done_;
  std::map<std::string, int> counters_;
};

}  // namespace

fs::path download_x(const XStatusRef& ref, const XOptions& opt) {
  Http http;
  json resp = fetch_status(http, opt, ref);
  const json& tweet = js::obj(resp, "tweet");
  std::string handle;
  for (char c : js::str(js::obj(tweet, "author"), "screen_name", ref.user))
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') handle += c;  // X handles: [A-Za-z0-9_]
  if (handle.empty()) handle = "unknown";
  const json& article = js::obj(tweet, "article");
  const bool is_article = !js::obj(article, "content").empty() || !js::obj(article, "content_state").empty();

  if (is_article) {
    std::string title = js::str(article, "title");
    std::string slug = slugify(title);
    fs::path dir = opt.out_root / "x" / handle / (ref.id + (slug.empty() ? "" : "-" + slug));
    std::cerr << "X Article: \"" << title << "\" by @" << handle << "\n";
    AssetStore assets(http, dir, opt.download_media);
    RenderedDoc doc = render_article(tweet, std::ref(assets));
    write_file(dir / "article.json", resp.dump(2));
    if (opt.write_md) write_file(dir / "article.md", doc.markdown);
    if (opt.write_html) write_file(dir / "article.html", doc.html);
    for (const auto& w : doc.warnings) std::cerr << "  warning: " << w << "\n";
    std::cerr << "  saved " << assets.count() << " media file(s) → " << dir.string() << "\n";
    return dir;
  }

  if (tweet.contains("article") && !tweet["article"].is_null()) {
    std::cerr << "note: this post links an X Article but its body was not returned; saving the post only\n";
  }

  fs::path dir = opt.out_root / "x" / handle / ref.id;
  std::cerr << "X post " << ref.id << " by @" << handle << "\n";

  // Quality caps / audio extraction are yt-dlp features; otherwise take the best mp4 directly.
  const bool has_video = !js::arr(js::obj(tweet, "media"), "videos").empty();
  const bool use_ytdlp = has_video && (opt.video.audio_only || opt.video.max_height);
  json t = tweet;
  if (use_ytdlp) {
    std::string url = "https://x.com/" + handle + "/status/" + ref.id;
    int rc = run_ytdlp(url, ytdlp_escape_path(dir) + "/video-%(playlist_index|1)s.%(ext)s", opt.video,
                       /*force_playlist=*/true);
    if (rc != 0) std::cerr << "  ! yt-dlp exited with " << rc << "\n";
    // Link whatever yt-dlp produced (local relative paths are passed through by AssetStore).
    json local = json::array();
    if (fs::exists(dir)) {
      // Only final media files (video-<n>.<ext>), not .info.json / subtitles / .fNNN fragments.
      static const std::regex final_media(R"(^video-(\d+)\.(mp4|webm|mkv|mov|m4a|mp3|opus|ogg)$)");
      std::vector<std::pair<long, std::string>> found;
      for (const auto& e : fs::directory_iterator(dir)) {
        std::string n = e.path().filename().string();
        std::smatch m;
        if (std::regex_match(n, m, final_media)) found.emplace_back(std::stol(m[1].str()), n);
      }
      std::sort(found.begin(), found.end());
      for (const auto& [idx, n] : found)
        local.push_back({{"url", n}, {"type", opt.video.audio_only ? "audio" : "video"}});
    }
    if (!t["media"].is_object()) t["media"] = json::object();
    t["media"]["videos"] = local;
  }
  AssetStore assets(http, dir, opt.download_media);
  RenderedDoc doc = render_post(t, std::ref(assets));
  write_file(dir / "post.json", resp.dump(2));
  if (opt.write_md) write_file(dir / "post.md", doc.markdown);
  if (opt.write_html) write_file(dir / "post.html", doc.html);
  std::cerr << "  saved " << assets.count() << " media file(s) → " << dir.string() << "\n";
  return dir;
}

}  // namespace mg
