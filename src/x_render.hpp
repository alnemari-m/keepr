#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mg {

struct MediaRef {
  std::string url;    // best-quality remote URL
  std::string kind;   // "image" | "video" | "gif"
  std::string role;   // "cover" | "body" | "post"
  std::string ext;    // suggested file extension, without dot
};

// Maps a remote media reference to the href written into the output
// (a relative local path once downloaded, or the remote URL).
using MediaResolver = std::function<std::string(const MediaRef&)>;

struct RenderedDoc {
  std::string title;
  std::string markdown;
  std::string html;
  std::vector<std::string> warnings;
};

// Full-resolution pbs.twimg.com URL ("...?format=jpg&name=orig").
MediaRef image_ref(const std::string& url, const std::string& role);

// Best mp4 variant found anywhere under `media_info` (videos / GIFs), if any.
std::optional<MediaRef> video_ref(const nlohmann::json& media_info, const std::string& role);

// `tweet` is the "tweet" object of an api.fxtwitter.com status response that carries "article".
RenderedDoc render_article(const nlohmann::json& tweet, const MediaResolver& resolve);

// Ordinary post (no article): text, photos, videos.
RenderedDoc render_post(const nlohmann::json& tweet, const MediaResolver& resolve);

}  // namespace mg
