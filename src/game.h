#pragma once
#include <windows.h>
#include <string>

struct Game {
  long long id, keyId;
  bool local, windows;
  std::wstring title, shortText, coverUrl, url, path;
  HBITMAP cover;
  Game() : id(0), keyId(0), local(false), windows(true), cover(NULL) {}
};
