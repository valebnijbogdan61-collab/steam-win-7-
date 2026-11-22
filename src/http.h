#pragma once
#include <string>

struct HttpResult {
  bool ok;
  int status;
  std::string body;
  std::wstring error;
};

typedef void (*ProgressFn)(unsigned long long done, unsigned long long total, void* user);

// GET з Bearer-токеном (WinHTTP, TLS 1.1/1.2 вмикається явно для Windows 7)
HttpResult HttpGet(const std::wstring& url, const std::string& bearerToken);

// Завантаження файлу на диск із колбеком прогресу
bool HttpDownload(const std::wstring& url, const std::wstring& destPath,
                  ProgressFn cb, void* user, std::wstring& error);
