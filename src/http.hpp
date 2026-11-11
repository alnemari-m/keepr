#pragma once
#include <filesystem>
#include <string>

typedef void CURL;

namespace mg {

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string content_type;
};

class Http {
 public:
  Http();
  ~Http();
  Http(const Http&) = delete;
  Http& operator=(const Http&) = delete;

  HttpResponse get(const std::string& url);
  // Streams to <out>.part then renames. Throws on network error or non-2xx.
  void download(const std::string& url, const std::filesystem::path& out,
                std::string* content_type = nullptr);

 private:
  CURL* curl_;
};

}  // namespace mg
