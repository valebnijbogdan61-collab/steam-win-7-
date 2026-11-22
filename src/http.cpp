#include "http.h"
#include "util.h"
#include <windows.h>
#include <winhttp.h>

#ifndef WINHTTP_OPTION_SECURE_PROTOCOLS
#define WINHTTP_OPTION_SECURE_PROTOCOLS 84
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 0x00000200
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif

namespace {

std::wstring LastErr(const wchar_t* what) {
  return std::wstring(what) + L" (code " + NumToW(GetLastError()) + L")";
}

class Request {
public:
  Request() : req(NULL), ses(NULL), con(NULL) {}
  ~Request() {
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
  }
  bool Start(const std::wstring& url, const std::wstring& headers, std::wstring& err);
  HINTERNET req;
private:
  Request(const Request&);
  Request& operator=(const Request&);
  HINTERNET ses, con;
};

bool Request::Start(const std::wstring& url, const std::wstring& headers, std::wstring& err) {
  wchar_t host[256], path[2048], extra[2048];
  URL_COMPONENTS uc;
  ZeroMemory(&uc, sizeof(uc));
  uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host;     uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;      uc.dwUrlPathLength = 2048;
  uc.lpszExtraInfo = extra;   uc.dwExtraInfoLength = 2048;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { err = LastErr(L"Bad URL"); return false; }

  ses = WinHttpOpen(L"Steam-Win7/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) { err = LastErr(L"WinHttpOpen failed"); return false; }

  // Windows 7: TLS 1.2 за замовчуванням вимкнений, вмикаємо вручну
  DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
  WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));

  con = WinHttpConnect(ses, host, uc.nPort, 0);
  if (!con) { err = LastErr(L"Connect failed"); return false; }

  std::wstring full = std::wstring(path) + std::wstring(extra);
  DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  req = WinHttpOpenRequest(con, L"GET", full.c_str(), NULL, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) { err = LastErr(L"OpenRequest failed"); return false; }

  BOOL sent = WinHttpSendRequest(req,
      headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
      headers.empty() ? 0 : (DWORD)-1L,
      WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (!sent) { err = LastErr(L"SendRequest failed (TLS 1.2 на Win7?)"); return false; }
  if (!WinHttpReceiveResponse(req, NULL)) { err = LastErr(L"No response"); return false; }
  return true;
}

} // namespace

HttpResult HttpGet(const std::wstring& url, const std::string& bearer) {
  HttpResult r;
  r.ok = false;
  r.status = 0;
  std::wstring headers;
  if (!bearer.empty()) headers = L"Authorization: Bearer " + Widen(bearer) + L"\r\n";

  Request q;
  if (!q.Start(url, headers, r.error)) return r;

  DWORD code = 0, sz = sizeof(code);
  WinHttpQueryHeaders(q.req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
  r.status = (int)code;

  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(q.req, &avail)) { r.error = LastErr(L"Read failed"); return r; }
    if (!avail) break;
    std::string buf(avail, '\0');
    DWORD got = 0;
    if (!WinHttpReadData(q.req, &buf[0], avail, &got)) { r.error = LastErr(L"Read failed"); return r; }
    r.body.append(buf, 0, got);
  }
  r.ok = true;
  return r;
}

bool HttpDownload(const std::wstring& url, const std::wstring& destPath,
                  ProgressFn cb, void* user, std::wstring& error) {
  Request q;
  if (!q.Start(url, L"", error)) return false;

  DWORD code = 0, sz = sizeof(code);
  WinHttpQueryHeaders(q.req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
  if (code != 200) { error = L"HTTP " + NumToW(code); return false; }

  DWORD len = 0; sz = sizeof(len);
  WinHttpQueryHeaders(q.req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &len, &sz, WINHTTP_NO_HEADER_INDEX);

  HANDLE f = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) { error = LastErr(L"Cannot create file"); return false; }

  unsigned long long done = 0;
  static char buf[65536];
  for (;;) {
    DWORD got = 0;
    if (!WinHttpReadData(q.req, buf, sizeof(buf), &got)) { error = LastErr(L"Read failed"); CloseHandle(f); return false; }
    if (!got) break;
    DWORD wr = 0;
    if (!WriteFile(f, buf, got, &wr, NULL) || wr != got) { error = LastErr(L"Write failed"); CloseHandle(f); return false; }
    done += got;
    if (cb) cb(done, len, user);
  }
  CloseHandle(f);
  return true;
}
