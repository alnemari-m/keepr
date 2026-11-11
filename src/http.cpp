#include "http.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <memory>
#include <stdexcept>

namespace mg {
namespace {

constexpr const char* kUserAgent = "keepr/0.1 (personal archiver; libcurl)";

struct CurlGlobal {
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
};

size_t write_string(char* p, size_t sz, size_t n, void* ud) {
  static_cast<std::string*>(ud)->append(p, sz * n);
  return sz * n;
}

size_t write_file(char* p, size_t sz, size_t n, void* ud) {
  return std::fwrite(p, sz, n, static_cast<std::FILE*>(ud)) * sz;
}

void common_opts(CURL* c, const std::string& url) {
  curl_easy_reset(c);
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);   // abort if stalled
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
  curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");   // gzip/br/zstd
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
}

std::string content_type_of(CURL* c) {
  char* ct = nullptr;
  curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &ct);
  return ct ? ct : "";
}

}  // namespace

Http::Http() {
  static CurlGlobal global;
  curl_ = curl_easy_init();
  if (!curl_) throw std::runtime_error("curl_easy_init failed");
}

Http::~Http() { curl_easy_cleanup(curl_); }

HttpResponse Http::get(const std::string& url) {
  HttpResponse r;
  common_opts(curl_, url);
  curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_string);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &r.body);
  CURLcode rc = curl_easy_perform(curl_);
  if (rc != CURLE_OK) throw std::runtime_error("GET " + url + ": " + curl_easy_strerror(rc));
  curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &r.status);
  r.content_type = content_type_of(curl_);
  return r;
}

void Http::download(const std::string& url, const std::filesystem::path& out, std::string* content_type) {
  std::filesystem::create_directories(out.parent_path());
  auto part = out;
  part += ".part";
  std::unique_ptr<std::FILE, int (*)(std::FILE*)> f(std::fopen(part.c_str(), "wb"), &std::fclose);
  if (!f) throw std::runtime_error("cannot write " + part.string());

  common_opts(curl_, url);
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_file);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, f.get());
  curl_easy_setopt(curl_, CURLOPT_FAILONERROR, 1L);
  CURLcode rc = curl_easy_perform(curl_);
  f.reset();
  if (rc != CURLE_OK) {
    std::filesystem::remove(part);
    throw std::runtime_error("download " + url + ": " + curl_easy_strerror(rc));
  }
  if (content_type) *content_type = content_type_of(curl_);
  std::filesystem::rename(part, out);
}

}  // namespace mg
