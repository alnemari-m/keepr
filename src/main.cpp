// keepr — download videos (X, TikTok, YouTube, Instagram, Reddit, ...) and
// archive X posts / X Articles (text, images, code, video) as Markdown + HTML.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "url.hpp"
#include "util.hpp"
#include "video.hpp"
#include "x_download.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* kUsage = R"(keepr 0.1 — video downloader + X Article archiver

usage: keepr [options] URL [URL...]

  X / Twitter
    x.com/<user>/status/<id>   X Article → article.md + article.html + assets/ (images, video, code, LaTeX)
                               normal post → post.md + post.html + photos/videos
  Anything else (TikTok, YouTube, Instagram, Reddit, Vimeo, ...)
                               video via yt-dlp

options:
  -o, --output DIR             output root (default: ./downloads)
  -i, --input FILE             read URLs from FILE (one per line, # comments)
  -q, --max-height N           cap video resolution, e.g. 720 or 1080
  -a, --audio-only             extract audio as mp3
      --playlist               follow playlists / profile pages (yt-dlp)
      --subs                   also download subtitles
      --info-json              write yt-dlp metadata JSON
      --cookies-from-browser B pass browser cookies to yt-dlp (chromium, firefox, ...)
      --cookies FILE           Netscape cookies.txt for yt-dlp
      --no-media               X: don't download images; link to remote URLs
      --format md|html|both    X: which documents to write (default: both)
      --yt-dlp PATH            yt-dlp executable (default: $KEEPR_YTDLP, else yt-dlp from PATH)
      --ytdlp-arg ARG          extra argument passed to yt-dlp (repeatable)
  -h, --help
)";

struct Args {
  std::vector<std::string> urls;
  mg::XOptions x;
  fs::path out = "downloads";
};

[[noreturn]] void die(const std::string& msg) {
  std::cerr << "keepr: " << msg << "\n(try --help)\n";
  std::exit(2);
}

Args parse(int argc, char** argv) {
  Args a;
  if (const char* y = std::getenv("KEEPR_YTDLP"); y && *y) a.x.video.ytdlp = y;
  auto need = [&](int& i) -> std::string {
    if (i + 1 >= argc) die(std::string("missing value for ") + argv[i]);
    return argv[++i];
  };
  for (int i = 1; i < argc; ++i) {
    std::string s = argv[i];
    if (s == "-h" || s == "--help") { std::cout << kUsage; std::exit(0); }
    else if (s == "-o" || s == "--output") a.out = need(i);
    else if (s == "-i" || s == "--input") {
      std::ifstream f(need(i));
      if (!f) die("cannot read input file");
      for (std::string line; std::getline(f, line);) {
        line = mg::trim(line);
        if (!line.empty() && line[0] != '#') a.urls.push_back(line);
      }
    }
    else if (s == "-q" || s == "--max-height") {
      try { a.x.video.max_height = std::stoi(need(i)); } catch (...) { die("--max-height needs a number"); }
    }
    else if (s == "-a" || s == "--audio-only") a.x.video.audio_only = true;
    else if (s == "--playlist") a.x.video.playlist = true;
    else if (s == "--subs") a.x.video.write_subs = true;
    else if (s == "--info-json") a.x.video.write_info_json = true;
    else if (s == "--cookies-from-browser") a.x.video.cookies_from_browser = need(i);
    else if (s == "--cookies") a.x.video.cookies_file = need(i);
    else if (s == "--no-media") a.x.download_media = false;
    else if (s == "--yt-dlp") a.x.video.ytdlp = need(i);
    else if (s == "--ytdlp-arg") a.x.video.extra_args.push_back(need(i));
    else if (s == "--format") {
      std::string f = need(i);
      if (f != "md" && f != "html" && f != "both") die("--format must be md, html or both");
      a.x.write_md = f != "html";
      a.x.write_html = f != "md";
    }
    else if (!s.empty() && s[0] == '-') die("unknown option " + s);
    else a.urls.push_back(s);
  }
  if (a.urls.empty()) { std::cerr << kUsage; std::exit(2); }
  a.x.out_root = a.out;
  return a;
}

int handle(const std::string& raw, const Args& a) {
  std::string url = raw.find("://") == std::string::npos ? "https://" + raw : raw;

  if (mg::is_x_bare_article_link(url)) {
    std::cerr << "error: " << raw << " is an x.com/i/article/ link, which holds the article id, not the post id.\n"
                 "  Open it, click the post's timestamp, and pass the x.com/<user>/status/<id> link instead.\n";
    return 1;
  }
  if (auto ref = mg::parse_x_status(url)) {
    mg::download_x(*ref, a.x);
    return 0;
  }
  std::string tmpl = mg::ytdlp_escape_path(a.out / "videos") +
                     "/%(extractor_key)s/%(uploader,channel,uploader_id|unknown)s/%(title).120B [%(id)s].%(ext)s";
  std::cerr << "video: " << url << "\n";
  return mg::run_ytdlp(url, tmpl, a.x.video);
}

}  // namespace

int main(int argc, char** argv) {
  Args a = parse(argc, argv);
  int failed = 0;
  for (const auto& u : a.urls) {
    try {
      if (handle(u, a) != 0) ++failed;
    } catch (const std::exception& e) {
      std::cerr << "error: " << e.what() << "\n";
      ++failed;
    }
  }
  if (a.urls.size() > 1) std::cerr << "\ndone: " << a.urls.size() - failed << " ok, " << failed << " failed\n";
  return failed ? 1 : 0;
}
