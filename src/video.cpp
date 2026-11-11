#include "video.hpp"

#include <spawn.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstring>
#include <iostream>

extern char** environ;

namespace mg {

std::string ytdlp_escape_path(const std::filesystem::path& p) {
  std::string out;
  for (char c : p.string()) {
    out.push_back(c);
    if (c == '%') out.push_back('%');
  }
  return out;
}

std::vector<std::string> ytdlp_argv(const std::string& url, const std::filesystem::path& out_template,
                                    const VideoOptions& opt, bool force_playlist) {
  std::vector<std::string> a = {opt.ytdlp, "--newline", "--no-mtime", "-o", out_template.string()};
  if (opt.audio_only) {
    a.insert(a.end(), {"-f", "ba/b", "-x", "--audio-format", "mp3", "--audio-quality", "0"});
  } else {
    std::string f = opt.max_height ? "bv*[height<=" + std::to_string(*opt.max_height) + "]+ba/b[height<=" +
                                         std::to_string(*opt.max_height) + "]/b"
                                   : "bv*+ba/b";
    a.insert(a.end(), {"-f", f, "--merge-output-format", "mp4"});
  }
  a.push_back(opt.playlist || force_playlist ? "--yes-playlist" : "--no-playlist");
  if (!opt.cookies_from_browser.empty()) a.insert(a.end(), {"--cookies-from-browser", opt.cookies_from_browser});
  if (!opt.cookies_file.empty()) a.insert(a.end(), {"--cookies", opt.cookies_file});
  if (opt.write_subs) a.insert(a.end(), {"--write-subs", "--write-auto-subs", "--sub-langs", "en.*,ar.*"});
  if (opt.write_info_json) a.push_back("--write-info-json");
  a.insert(a.end(), opt.extra_args.begin(), opt.extra_args.end());
  a.push_back("--");  // url can never be parsed as an option
  a.push_back(url);
  return a;
}

int run_ytdlp(const std::string& url, const std::filesystem::path& out_template, const VideoOptions& opt,
              bool force_playlist) {
  auto args = ytdlp_argv(url, out_template, opt, force_playlist);
  std::vector<char*> argv;
  for (auto& s : args) argv.push_back(s.data());
  argv.push_back(nullptr);

  pid_t pid;
  int rc = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
  if (rc != 0) {
    std::cerr << "error: cannot run '" << opt.ytdlp << "': " << std::strerror(rc)
              << "\n  install it with: sudo pacman -S yt-dlp   (or: pipx install yt-dlp)\n";
    return 127;
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return 1;
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

}  // namespace mg
