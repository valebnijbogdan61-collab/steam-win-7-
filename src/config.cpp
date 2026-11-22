#include "config.h"
#include "util.h"
#include <windows.h>

std::wstring ExeDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
  std::wstring p(buf, n);
  size_t s = p.find_last_of(L"\\/");
  return s == std::wstring::npos ? std::wstring(L".") : p.substr(0, s);
}

std::wstring ConfigPath() { return ExeDir() + L"\\config.ini"; }
static std::wstring InstalledIni() { return ExeDir() + L"\\installed.ini"; }

std::string LoadApiKey() {
  wchar_t buf[512] = {0};
  GetPrivateProfileStringW(L"itch", L"api_key", L"", buf, 512, ConfigPath().c_str());
  std::string out;
  for (wchar_t* p = buf; *p; ++p)
    if (*p > 32 && *p < 127) out += (char)*p;
  return out;
}

void SaveApiKey(const std::string& key) {
  WritePrivateProfileStringW(L"itch", L"api_key", Widen(key).c_str(), ConfigPath().c_str());
}

std::wstring GetInstalledPath(long long id) {
  wchar_t buf[1024] = {0};
  GetPrivateProfileStringW(L"installed", LLToW(id).c_str(), L"", buf, 1024, InstalledIni().c_str());
  return std::wstring(buf);
}

void SetInstalledPath(long long id, const std::wstring& path) {
  WritePrivateProfileStringW(L"installed", LLToW(id).c_str(), path.c_str(), InstalledIni().c_str());
}

std::vector<std::wstring> LoadLocalGames() {
  std::vector<std::wstring> v;
  std::wstring ini = InstalledIni();
  UINT n = GetPrivateProfileIntW(L"local", L"count", 0, ini.c_str());
  for (UINT i = 0; i < n && i < 500; ++i) {
    wchar_t buf[1024] = {0};
    GetPrivateProfileStringW(L"local", (L"p" + NumToW(i)).c_str(), L"", buf, 1024, ini.c_str());
    if (buf[0]) v.push_back(buf);
  }
  return v;
}

void SaveLocalGames(const std::vector<std::wstring>& paths) {
  std::wstring ini = InstalledIni();
  WritePrivateProfileStringW(L"local", L"count", NumToW((unsigned long)paths.size()).c_str(), ini.c_str());
  for (size_t i = 0; i < paths.size(); ++i)
    WritePrivateProfileStringW(L"local", (L"p" + NumToW((unsigned long)i)).c_str(), paths[i].c_str(), ini.c_str());
}
