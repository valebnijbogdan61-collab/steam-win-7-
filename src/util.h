#pragma once
#include <string>
#include <stdio.h>
#include <wchar.h>

inline std::wstring NumToW(unsigned long n) {
  wchar_t b[32];
  _snwprintf(b, 32, L"%lu", n);
  return std::wstring(b);
}

inline std::wstring LLToW(long long n) {
  wchar_t b[32];
  _snwprintf(b, 32, L"%I64d", n);
  return std::wstring(b);
}

// ASCII -> wide (для API-ключа та заголовків)
inline std::wstring Widen(const std::string& s) {
  std::wstring w;
  for (size_t i = 0; i < s.size(); ++i) w += (wchar_t)(unsigned char)s[i];
  return w;
}
