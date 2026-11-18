#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mg {

struct VideoOptions {
  std::string ytdlp = "yt-dlp";            // executable name or path
  std::optional<int> max_height;           // e.g. 720
  bool audio_only = false;                 // extract mp3
  bool playlist = false;                   // follow playlists / profiles
  std::string cookies_from_browser;        // e.g. "chromium", "firefox"
  std::string cookies_file;                // Netscape cookies.txt
  bool write_subs = false;
  bool write_info_json = false;
  bool pace = false;                       // wait between requests/downloads so sites do not rate-limit a batch
  std::string clip;                        // yt-dlp section, e.g. "*0-1:00"; empty = whole video
  std::vector<std::string> extra_args;     // passed through verbatim
};

// yt-dlp treats '%' in -o as a template field; escape it in literal directory parts.
std::string ytdlp_escape_path(const std::filesystem::path& p);

// "1:00" (first minute) or "2:30-3:30" / "90-inf" → yt-dlp section "*0-1:00" / "*2:30-3:30".
// nullopt if the text is not a time or time range.
std::optional<std::string> clip_section(const std::string& range);

// Builds the yt-dlp argv (exposed for testing).
std::vector<std::string> ytdlp_argv(const std::string& url, const std::filesystem::path& out_template,
                                    const VideoOptions& opt, bool force_playlist = false);

// Runs yt-dlp without a shell; output streams to the terminal. Returns its exit code,
// or 127 if the executable was not found.
int run_ytdlp(const std::string& url, const std::filesystem::path& out_template, const VideoOptions& opt,
              bool force_playlist = false);

}  // namespace mg
