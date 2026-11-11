#pragma once
#include <filesystem>
#include <string>

#include "url.hpp"
#include "video.hpp"

namespace mg {

struct XOptions {
  std::filesystem::path out_root;
  bool download_media = true;   // false: keep remote image URLs in the output
  bool write_md = true;
  bool write_html = true;
  bool videos_via_ytdlp = true; // plain posts: use yt-dlp for videos (best quality), fallback to direct mp4
  std::string api_base = "https://api.fxtwitter.com";
  VideoOptions video;
};

// Downloads an X post or X Article into <out_root>/x/<handle>/<id>[-slug]/.
// Returns the output directory.
std::filesystem::path download_x(const XStatusRef& ref, const XOptions& opt);

}  // namespace mg
