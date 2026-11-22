#include "itch.h"
#include "http.h"
#include "json.h"
#include "util.h"
#include <windows.h>
#include <set>

namespace {

std::wstring Utf8ToW(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
  return w;
}

std::wstring UrlEncode(const std::wstring& w) {
  std::string u;
  if (!w.empty()) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    u.assign(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &u[0], n, NULL, NULL);
  }
  static const char* hex = "0123456789ABCDEF";
  std::wstring out;
  for (size_t i = 0; i < u.size(); ++i) {
    unsigned char c = (unsigned char)u[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.' || c == '~')
      out += (wchar_t)c;
    else { out += L'%'; out += (wchar_t)hex[c >> 4]; out += (wchar_t)hex[c & 15]; }
  }
  return out;
}

bool ParseGame(const Json& j, Game& g) {
  g.id = j["id"].I();
  if (!g.id) return false;
  g.title = Utf8ToW(j["title"].S());
  g.shortText = Utf8ToW(j["short_text"].S());
  g.coverUrl = Utf8ToW(j["cover_url"].S());
  g.url = Utf8ToW(j["url"].S());
  g.windows = j["p_windows"].B();
  return true;
}

bool GetJson(const std::wstring& url, const std::string& key, Json& j, std::wstring& err) {
  HttpResult h = HttpGet(url, key);
  if (!h.ok) { err = h.error; return false; }
  if (h.status == 401 || h.status == 403) { err = L"Invalid API key"; return false; }
  if (h.status != 200) { err = L"itch.io: HTTP " + NumToW((unsigned long)h.status); return false; }
  if (!JsonParse(h.body, j)) { err = L"Unexpected response"; return false; }
  return true;
}

} // namespace

ItchProfile ItchGetProfile(const std::string& key) {
  ItchProfile r;
  if (key.empty()) { r.error = L"No API key"; return r; }
  HttpResult h = HttpGet(L"https://api.itch.io/profile", key);
  if (!h.ok) { r.error = h.error; return r; }
  if (h.status == 401 || h.status == 403) { r.badKey = true; r.error = L"Invalid API key"; return r; }
  if (h.status != 200) { r.error = L"itch.io: HTTP " + NumToW((unsigned long)h.status); return r; }
  Json j;
  if (!JsonParse(h.body, j)) { r.error = L"Unexpected response"; return r; }
  std::string name = j["user"]["display_name"].S();
  if (name.empty()) name = j["user"]["username"].S();
  if (name.empty()) { r.error = L"Unexpected response"; return r; }
  r.name = Utf8ToW(name);
  r.url = Utf8ToW(j["user"]["url"].S());
  r.ok = true;
  return r;
}

bool ItchLoadLibrary(const std::string& key, std::vector<Game>& out, std::wstring& err) {
  std::set<long long> seen;
  bool anyOk = false;
  std::wstring firstErr;

  for (int page = 1; page <= 20; ++page) {
    Json j;
    std::wstring e;
    if (!GetJson(L"https://api.itch.io/profile/owned-keys?page=" + NumToW(page), key, j, e)) { firstErr = e; break; }
    anyOk = true;
    const Json& keys = j["owned_keys"];
    if (keys.type != Json::Arr || keys.arr.empty()) break;
    for (size_t i = 0; i < keys.arr.size(); ++i) {
      Game g;
      if (!ParseGame(keys.arr[i]["game"], g)) continue;
      g.keyId = keys.arr[i]["id"].I();
      if (seen.insert(g.id).second) out.push_back(g);
    }
    long long per = j["per_page"].I();
    if (per > 0 && (long long)keys.arr.size() < per) break;
  }

  Json j;
  std::wstring e;
  if (GetJson(L"https://api.itch.io/profile/games", key, j, e)) {
    anyOk = true;
    const Json& gs = j["games"];
    for (size_t i = 0; i < gs.arr.size(); ++i) {
      Game g;
      if (!ParseGame(gs.arr[i], g)) continue;
      if (seen.insert(g.id).second) out.push_back(g);
    }
  } else if (firstErr.empty()) firstErr = e;

  if (!anyOk) { err = firstErr.empty() ? L"Cannot load library" : firstErr; return false; }
  return true;
}

bool ItchSearch(const std::string& key, const std::wstring& query, std::vector<Game>& out, std::wstring& err) {
  std::wstring url = L"https://itch.io/api/1/" + Widen(key) + L"/search/games?query=" + UrlEncode(query) + L"&page=1";
  Json j;
  if (!GetJson(url, "", j, err)) return false;
  const Json& gs = j["games"];
  for (size_t i = 0; i < gs.arr.size(); ++i) {
    Game g;
    if (ParseGame(gs.arr[i], g)) out.push_back(g);
  }
  return true;
}

bool ItchResolveDownload(const std::string& key, long long gameId, long long keyId,
                         std::wstring& url, std::wstring& filename, std::wstring& err) {
  std::wstring u = L"https://api.itch.io/games/" + LLToW(gameId) + L"/uploads";
  if (keyId) u += L"?download_key_id=" + LLToW(keyId);
  Json j;
  if (!GetJson(u, key, j, err)) return false;
  const Json& ups = j["uploads"];
  if (ups.type != Json::Arr || ups.arr.empty()) { err = L"No downloadable files for this game"; return false; }
  size_t pick = 0;
  for (size_t i = 0; i < ups.arr.size(); ++i)
    if (ups.arr[i]["p_windows"].B()) { pick = i; break; }
  long long uid = ups.arr[pick]["id"].I();
  filename = Utf8ToW(ups.arr[pick]["filename"].S());
  if (filename.empty()) filename = L"download.bin";
  url = L"https://api.itch.io/uploads/" + LLToW(uid) + L"/download?api_key=" + Widen(key);
  if (keyId) url += L"&download_key_id=" + LLToW(keyId);
  return true;
}
