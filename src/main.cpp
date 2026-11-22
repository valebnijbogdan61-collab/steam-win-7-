// Steam (Win7 edition): головне вікно. Win32 + GDI, без сторонніх бібліотек.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include "ui.h"
#include "util.h"
#include "config.h"
#include "itch.h"
#include "http.h"
#include "img.h"
#include "game.h"
#include "account.h"
#include "zip.h"

using namespace theme;
using ui::R;

enum Page { PAGE_STORE = 0, PAGE_LIBRARY = 1, PAGE_COMMUNITY = 2, PAGE_PROFILE = 3,
            PAGE_DOWNLOADS = 4, PAGE_LOGIN = 5, PAGE_GAME = 6, PAGE_ACCOUNT = 7 };

enum Hit { H_NONE = 0, H_BACK = 1, H_TAB0 = 10, H_ADD = 20, H_DL = 21, H_FRIENDS = 22,
           H_MENU0 = 30, H_SIGNIN = 40, H_SIGNOUT = 41, H_OPENPROFILE = 42, H_INSTALL = 43,
           H_OPENWEB = 44, H_SEARCHBTN = 45, H_CARD0 = 50, H_CREATEACC = 46,
           H_SIDE = 1000, H_TILE = 100000 };

static const int MENU_H = 28, NAV_H = 44, BOTTOM_H = 34, SIDEBAR_W = 260;
static const int TILE_W = 220, TILE_IMG_H = 175, TILE_H = 207;
static const UINT WM_PROFILE = WM_APP + 1, WM_LIBRARY = WM_APP + 2, WM_LIBERR = WM_APP + 3,
                  WM_STORE = WM_APP + 4, WM_COVER = WM_APP + 5, WM_DLPROG = WM_APP + 6,
                  WM_DLDONE = WM_APP + 7, WM_ENTER = WM_APP + 8;
static const UINT CENTER = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
static const UINT LEFTV = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
static const wchar_t* kMenus[5] = { L"Steam", L"View", L"Friends", L"Games", L"Help" };

struct Dl {
  bool busy, extracting; int pct; long long gameId;
  unsigned long long bytesDone, bytesTotal;
  double speedKBs;
  DWORD lastTick; unsigned long long lastBytes;
  std::wstring title, status;
  Dl() : busy(false), extracting(false), pct(0), gameId(0), bytesDone(0), bytesTotal(0),
         speedKBs(0), lastTick(0), lastBytes(0) {}
};

struct App {
  HWND hwnd, search, keyEdit, accUser, accPass, accPass2;
  HFONT fMenu, fNav, fSmall, fSmallB, fBig, fMid;
  Page page, prevPage;
  int hover, curSrc, curIdx, scroll, sideScroll, savedScroll;
  bool tracking, libLoading, storeLoading, storeLoaded;
  unsigned gen[2];
  std::string apiKey;
  std::wstring username, profileUrl, status, loginErr, libErr, storeErr, gameMsg, accErr;
  std::vector<Game> lib, store;
  Dl dl;
} g;

static HWND g_miniWnd = NULL;
static int g_miniHover = 0;

struct RectBlock {
  RECT menu, nav, body, bottom, side, mainArea, searchBox, sideList, gridArea,
       back, fwd, add, dl, friends, btnSearch, loginBox, keyBox, btnSignIn,
       btnSignOut, btnProfile, cover, btnInstall, btnWeb,
       accBox, accUserBox, accPassBox, accPass2Box, btnCreateAcc,
       hero, reqBox;
  RECT menuItem[5], tab[4], cards[3];
};

struct Layout : RectBlock {
  std::vector<RECT> tiles, sideRows;
  std::vector<int> tileIdx, sideIdx;
  Layout() { memset(static_cast<RectBlock*>(this), 0, sizeof(RectBlock)); }
};

// ---------- допоміжне ----------

static std::wstring Lower(std::wstring s) {
  if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
  return s;
}

static std::wstring SafeName(std::wstring s) {
  for (size_t i = 0; i < s.size(); ++i)
    if (wcschr(L"\\/:*?\"<>|", s[i])) s[i] = L'_';
  return s;
}

static bool IsExe(const std::wstring& p) {
  std::wstring l = Lower(p);
  return l.size() > 4 && l.compare(l.size() - 4, 4, L".exe") == 0;
}

static std::wstring DirOf(const std::wstring& p) {
  size_t s = p.find_last_of(L"\\/");
  return s == std::wstring::npos ? p : p.substr(0, s);
}

static void FreeGames(std::vector<Game>& v) {
  for (size_t i = 0; i < v.size(); ++i) if (v[i].cover) DeleteObject(v[i].cover);
  v.clear();
}

static void Redraw() { InvalidateRect(g.hwnd, NULL, FALSE); }

static Game* GameById(long long id) {
  for (size_t i = 0; i < g.lib.size(); ++i) if (g.lib[i].id == id) return &g.lib[i];
  for (size_t i = 0; i < g.store.size(); ++i) if (g.store[i].id == id) return &g.store[i];
  return NULL;
}

static Game* CurGame() {
  std::vector<Game>& v = g.curSrc ? g.store : g.lib;
  if (g.curIdx < 0 || g.curIdx >= (int)v.size()) return NULL;
  return &v[g.curIdx];
}

static std::wstring ActionLabel(const Game& gm) {
  if (gm.path.empty()) return L"INSTALL";
  return IsExe(gm.path) ? L"PLAY" : L"OPEN FOLDER";
}

static std::wstring TabLabel(int i) {
  if (i == 0) return L"STORE";
  if (i == 1) return L"LIBRARY";
  if (i == 2) return L"COMMUNITY";
  if (g.username.empty()) return L"OFFLINE";
  std::wstring u = g.username;
  CharUpperBuffW(&u[0], (DWORD)u.size());
  return u;
}

static std::vector<int> FilterLib() {
  std::vector<int> r;
  std::wstring q;
  if (g.page == PAGE_LIBRARY) {
    wchar_t b[256] = {0};
    GetWindowTextW(g.search, b, 256);
    q = Lower(b);
  }
  for (size_t i = 0; i < g.lib.size(); ++i)
    if (q.empty() || Lower(g.lib[i].title).find(q) != std::wstring::npos) r.push_back((int)i);
  return r;
}

// ---------- мережеві потоки ----------

struct KeyArg { std::string key; HWND hwnd; };

static DWORD WINAPI LoaderThread(LPVOID p) {
  KeyArg* a = (KeyArg*)p;
  ItchProfile pr = ItchGetProfile(a->key);
  if (!pr.ok) {
    PostMessageW(a->hwnd, WM_PROFILE, pr.badKey ? 2 : 0, (LPARAM)new std::wstring(pr.error));
  } else {
    PostMessageW(a->hwnd, WM_PROFILE, 1, (LPARAM)new std::wstring(pr.name + L"\n" + pr.url));
    std::vector<Game>* lib = new std::vector<Game>();
    std::wstring err;
    if (ItchLoadLibrary(a->key, *lib, err)) PostMessageW(a->hwnd, WM_LIBRARY, 0, (LPARAM)lib);
    else { delete lib; PostMessageW(a->hwnd, WM_LIBERR, 0, (LPARAM)new std::wstring(err)); }
  }
  delete a;
  return 0;
}

static void StartLoader() {
  g.libLoading = true; g.libErr.clear(); g.status = L"Connecting...";
  KeyArg* a = new KeyArg; a->key = g.apiKey; a->hwnd = g.hwnd;
  HANDLE h = CreateThread(NULL, 0, LoaderThread, a, 0, NULL);
  if (h) CloseHandle(h); else delete a;
}

struct SearchArg { std::string key; std::wstring query; HWND hwnd; unsigned seq; };

static DWORD WINAPI SearchThread(LPVOID p) {
  SearchArg* a = (SearchArg*)p;
  std::vector<Game>* v = new std::vector<Game>();
  std::wstring err;
  bool ok = ItchSearch(a->key, a->query, *v, err);
  WPARAM wp = ((WPARAM)a->seq << 1) | (ok ? 1 : 0);
  if (ok) PostMessageW(a->hwnd, WM_STORE, wp, (LPARAM)v);
  else { delete v; PostMessageW(a->hwnd, WM_STORE, wp, (LPARAM)new std::wstring(err)); }
  delete a;
  return 0;
}

static void StartSearch(const std::wstring& q) {
  if (g.apiKey.empty()) return;
  g.storeLoading = true; g.storeLoaded = true; g.storeErr.clear();
  SearchArg* a = new SearchArg;
  a->key = g.apiKey; a->query = q; a->hwnd = g.hwnd; a->seq = ++g.gen[1];
  HANDLE h = CreateThread(NULL, 0, SearchThread, a, 0, NULL);
  if (h) CloseHandle(h); else delete a;
  Redraw();
}

struct CoverJob { HWND hwnd; int src; unsigned gen; std::vector<std::wstring> urls; LONG next; LONG refs; };

static DWORD WINAPI CoverWorker(LPVOID p) {
  CoverJob* j = (CoverJob*)p;
  for (;;) {
    LONG i = InterlockedIncrement(&j->next) - 1;
    if (i >= (LONG)j->urls.size()) break;
    if (j->urls[i].empty()) continue;
    HttpResult h = HttpGet(j->urls[i], "");
    if (!h.ok || h.status != 200) continue;
    HBITMAP bm = DecodeImage(h.body);
    if (!bm) continue;
    WPARAM wp = ((WPARAM)(j->gen & 0x7FFF) << 13) | ((WPARAM)j->src << 12) | (WPARAM)(i & 0xFFF);
    if (!PostMessageW(j->hwnd, WM_COVER, wp, (LPARAM)bm)) DeleteObject(bm);
  }
  if (InterlockedDecrement(&j->refs) == 0) delete j;
  return 0;
}

static void StartCovers(int src, const std::vector<Game>& v) {
  CoverJob* j = new CoverJob;
  j->hwnd = g.hwnd; j->src = src; j->gen = g.gen[src]; j->next = 0; j->refs = 3;
  for (size_t i = 0; i < v.size() && i < 4000; ++i) j->urls.push_back(v[i].coverUrl);
  for (int t = 0; t < 3; ++t) {
    HANDLE h = CreateThread(NULL, 0, CoverWorker, j, 0, NULL);
    if (h) CloseHandle(h); else InterlockedDecrement(&j->refs);
  }
}

struct InstArg { std::string key; HWND hwnd; long long gameId, keyId; };
struct DlCtx { HWND hwnd; int last; DWORD lastPost; };

static void OnProg(unsigned long long done, unsigned long long total, void* u) {
  DlCtx* c = (DlCtx*)u;
  int pct = total ? (int)(done * 100 / total) : 0;
  DWORD now = GetTickCount();
  bool finished = total && done >= total;
  if (pct == c->last && (now - c->lastPost) < 200 && !finished) return;
  c->last = pct; c->lastPost = now;
  unsigned long long* buf = new unsigned long long[2];
  buf[0] = done; buf[1] = total;
  if (!PostMessageW(c->hwnd, WM_DLPROG, pct, (LPARAM)buf)) delete[] buf;
}

static bool IsZip(const std::wstring& p) {
  return p.size() > 4 && _wcsicmp(p.c_str() + p.size() - 4, L".zip") == 0;
}

static DWORD WINAPI InstallThread(LPVOID p) {
  InstArg* a = (InstArg*)p;
  std::wstring url, fname, err, dest;
  bool ok = ItchResolveDownload(a->key, a->gameId, a->keyId, url, fname, err);
  std::wstring dir;
  if (ok) {
    dir = ExeDir() + L"\\games";
    CreateDirectoryW(dir.c_str(), NULL);
    dir += L"\\" + LLToW(a->gameId);
    CreateDirectoryW(dir.c_str(), NULL);
    dest = dir + L"\\" + SafeName(fname);
    DlCtx c = { a->hwnd, -1, 0 };
    ok = HttpDownload(url, dest, OnProg, &c, err);
  }
  if (ok && IsZip(dest)) {
    PostMessageW(a->hwnd, WM_DLPROG, 101, 0);  // 101 = "unpacking" sentinel
    std::wstring extractErr;
    if (ExtractZip(dest, dir, extractErr)) {
      DeleteFileW(dest.c_str());
      std::wstring exe = FindMainExe(dir);
      dest = exe.empty() ? dir : exe;
    }
    // якщо розпакування не вдалось, лишаємо сам .zip: відкриється папка, користувач розпакує сам
  }
  PostMessageW(a->hwnd, WM_DLDONE, ok ? 1 : 0, (LPARAM)new std::wstring(ok ? dest : err));
  delete a;
  return 0;
}

static void StartInstall(const Game& gm) {
  if (g.dl.busy) { g.gameMsg = L"Another download is in progress"; Redraw(); return; }
  g.dl.busy = true; g.dl.extracting = false; g.dl.pct = 0; g.dl.gameId = gm.id; g.dl.title = gm.title;
  g.dl.status = L"Preparing..."; g.dl.bytesDone = 0; g.dl.bytesTotal = 0; g.dl.speedKBs = 0;
  g.dl.lastTick = GetTickCount(); g.dl.lastBytes = 0; g.gameMsg.clear();
  InstArg* a = new InstArg;
  a->key = g.apiKey; a->hwnd = g.hwnd; a->gameId = gm.id; a->keyId = gm.keyId;
  HANDLE h = CreateThread(NULL, 0, InstallThread, a, 0, NULL);
  if (h) CloseHandle(h); else { delete a; g.dl.busy = false; }
  Redraw();
}

static void SetPathFor(long long id, const std::wstring& path) {
  for (size_t i = 0; i < g.lib.size(); ++i) if (g.lib[i].id == id) g.lib[i].path = path;
  for (size_t i = 0; i < g.store.size(); ++i) if (g.store[i].id == id) g.store[i].path = path;
  SetInstalledPath(id, path);
}

// ---------- розкладка ----------

static int TextWidth(HDC dc, HFONT f, const std::wstring& s) { return ui::TextW(dc, f, s); }

static void BuildGrid(Layout& lo, const std::vector<int>& idx) {
  const RECT& a = lo.gridArea;
  int aw = a.right - a.left;
  int cols = (aw - 24) / (TILE_W + 16);
  if (cols < 1) cols = 1;
  int n = (int)idx.size();
  int rows = (n + cols - 1) / cols;
  int contentH = rows * (TILE_H + 16) + 16;
  int ah = a.bottom - a.top;
  int maxS = contentH > ah ? contentH - ah : 0;
  if (g.scroll > maxS) g.scroll = maxS;
  if (g.scroll < 0) g.scroll = 0;
  for (int k = 0; k < n; ++k) {
    int row = k / cols, col = k % cols;
    int x = a.left + 24 + col * (TILE_W + 16);
    int y = a.top + 16 + row * (TILE_H + 16) - g.scroll;
    if (y + TILE_H < a.top || y > a.bottom) continue;
    lo.tiles.push_back(R(x, y, x + TILE_W, y + TILE_H));
    lo.tileIdx.push_back(idx[k]);
  }
}

static Layout Compute(HDC dc, const RECT& c) {
  Layout lo;
  int w = c.right, h = c.bottom;
  lo.menu   = R(0, 0, w, MENU_H);
  lo.nav    = R(0, MENU_H, w, MENU_H + NAV_H);
  lo.bottom = R(0, h - BOTTOM_H, w, h);
  lo.body   = R(0, lo.nav.bottom, w, lo.bottom.top);
  lo.side   = R(0, lo.body.top, SIDEBAR_W, lo.body.bottom);
  lo.mainArea = R(SIDEBAR_W, lo.body.top, w, lo.body.bottom);

  int x = 10;
  for (int i = 0; i < 5; i++) {
    int tw = TextWidth(dc, g.fMenu, kMenus[i]) + 22;
    lo.menuItem[i] = R(x, 0, x + tw, MENU_H);
    x += tw;
  }
  int ny = lo.nav.top + (NAV_H - 26) / 2;
  lo.back = R(14, ny, 40, ny + 26);
  lo.fwd  = R(42, ny, 68, ny + 26);
  x = 84;
  for (int i = 0; i < 4; i++) {
    int tw = TextWidth(dc, g.fNav, TabLabel(i)) + 34;
    lo.tab[i] = R(x, lo.nav.top, x + tw, lo.nav.bottom);
    x += tw;
  }
  lo.dl      = R(w / 2 - 160, lo.bottom.top, w / 2 + 160, h);
  lo.friends = R(w - 180, lo.bottom.top, w, h);

  int bt = lo.body.top;
  if (g.page == PAGE_ACCOUNT) {
    int cx = w / 2, cy = (lo.body.top + lo.body.bottom) / 2;
    lo.accBox = R(cx - 230, cy - 200, cx + 230, cy + 200);
    lo.accUserBox  = R(lo.accBox.left + 30, lo.accBox.top + 140, lo.accBox.right - 30, lo.accBox.top + 174);
    lo.accPassBox  = R(lo.accBox.left + 30, lo.accBox.top + 196, lo.accBox.right - 30, lo.accBox.top + 230);
    lo.accPass2Box = R(lo.accBox.left + 30, lo.accBox.top + 252, lo.accBox.right - 30, lo.accBox.top + 286);
    lo.btnCreateAcc = R(lo.accBox.left + 30, lo.accBox.top + 320, lo.accBox.right - 30, lo.accBox.top + 364);
  } else if (g.page == PAGE_LIBRARY) {
    lo.searchBox = R(10, lo.side.top + 10, SIDEBAR_W - 10, lo.side.top + 38);
    std::vector<int> f = FilterLib();
    int top = lo.searchBox.bottom + 40, rh = 26;
    lo.sideList = R(0, top, SIDEBAR_W - 1, lo.side.bottom);
    int visH = lo.side.bottom - top, total = (int)f.size() * rh;
    int maxS = total > visH ? total - visH : 0;
    if (g.sideScroll > maxS) g.sideScroll = maxS;
    if (g.sideScroll < 0) g.sideScroll = 0;
    for (size_t k = 0; k < f.size(); ++k) {
      int y = top + (int)k * rh - g.sideScroll;
      if (y + rh < top || y > lo.side.bottom) continue;
      lo.sideRows.push_back(R(0, y, SIDEBAR_W - 1, y + rh));
      lo.sideIdx.push_back(f[k]);
    }
    lo.gridArea = R(SIDEBAR_W, bt + 36, w, lo.body.bottom);
    BuildGrid(lo, f);
  } else if (g.page == PAGE_STORE) {
    lo.searchBox = R(24, bt + 12, w - 190, bt + 44);
    lo.btnSearch = R(w - 174, bt + 12, w - 24, bt + 44);
    lo.gridArea = R(0, bt + 56, w, lo.body.bottom);
    std::vector<int> all;
    for (size_t i = 0; i < g.store.size(); ++i) all.push_back((int)i);
    BuildGrid(lo, all);
  } else if (g.page == PAGE_GAME) {
    lo.hero = R(0, bt, w, bt + 220);
    int colW = (w * 60) / 100;
    if (colW < 360) colW = w > 400 ? w - 40 : w;
    int colSplit = 40 + colW;
    lo.cover = lo.hero;  // банер малюється з обкладинки гри (розтягнутої й затемненої)
    lo.btnInstall = R(40, lo.hero.bottom + 108, 40 + 180, lo.hero.bottom + 150);
    lo.btnWeb = R(230, lo.hero.bottom + 108, 400, lo.hero.bottom + 150);
    lo.reqBox = R(colSplit + 20, lo.hero.bottom + 24, w - 24, lo.body.bottom - 16);
  } else if (g.page == PAGE_LOGIN) {
    int cx = w / 2, cy = (lo.body.top + lo.body.bottom) / 2;
    lo.loginBox = R(cx - 220, cy - 160, cx + 220, cy + 160);
    lo.keyBox = R(lo.loginBox.left + 30, lo.loginBox.top + 130, lo.loginBox.right - 30, lo.loginBox.top + 164);
    lo.btnSignIn = R(lo.loginBox.left + 30, lo.loginBox.top + 230, lo.loginBox.right - 30, lo.loginBox.top + 274);
  } else if (g.page == PAGE_PROFILE) {
    lo.btnProfile = R(40, bt + 130, 300, bt + 174);
    lo.btnSignOut = R(40, bt + 190, 300, bt + 234);
  } else if (g.page == PAGE_COMMUNITY) {
    for (int i = 0; i < 3; i++) lo.cards[i] = R(40 + i * 280, bt + 90, 40 + i * 280 + 260, bt + 230);
  }
  return lo;
}

static Layout CurLayout(HWND hwnd) {
  RECT c; GetClientRect(hwnd, &c);
  HDC dc = GetDC(hwnd);
  Layout lo = Compute(dc, c);
  ReleaseDC(hwnd, dc);
  return lo;
}

static bool In(const RECT& r, int x, int y) { POINT p = { x, y }; return PtInRect(&r, p) != 0; }

static int HitTest(HWND hwnd, int x, int y) {
  Layout lo = CurLayout(hwnd);
  for (int i = 0; i < 5; i++) if (In(lo.menuItem[i], x, y)) return H_MENU0 + i;
  if (In(lo.back, x, y)) return H_BACK;
  for (int i = 0; i < 4; i++) if (In(lo.tab[i], x, y)) return H_TAB0 + i;
  if (In(lo.dl, x, y)) return H_DL;
  if (In(lo.friends, x, y)) return H_FRIENDS;
  switch (g.page) {
    case PAGE_ACCOUNT: if (In(lo.btnCreateAcc, x, y)) return H_CREATEACC; break;
    case PAGE_LOGIN: if (In(lo.btnSignIn, x, y)) return H_SIGNIN; break;
    case PAGE_PROFILE:
      if (In(lo.btnProfile, x, y)) return H_OPENPROFILE;
      if (In(lo.btnSignOut, x, y)) return H_SIGNOUT;
      break;
    case PAGE_COMMUNITY:
      for (int i = 0; i < 3; i++) if (In(lo.cards[i], x, y)) return H_CARD0 + i;
      break;
    case PAGE_GAME: {
      Game* gm = CurGame();
      if (In(lo.btnInstall, x, y)) return H_INSTALL;
      if (gm && !gm->local && In(lo.btnWeb, x, y)) return H_OPENWEB;
      break;
    }
    case PAGE_STORE:
      if (In(lo.btnSearch, x, y)) return H_SEARCHBTN;
      // fallthrough
    case PAGE_LIBRARY:
      if (In(lo.gridArea, x, y))
        for (size_t k = 0; k < lo.tiles.size(); ++k) if (In(lo.tiles[k], x, y)) return H_TILE + lo.tileIdx[k];
      if (g.page == PAGE_LIBRARY && In(lo.sideList, x, y))
        for (size_t k = 0; k < lo.sideRows.size(); ++k) if (In(lo.sideRows[k], x, y)) return H_SIDE + lo.sideIdx[k];
      break;
    default: break;
  }
  return H_NONE;
}

// ---------- малювання ----------

static void DrawCoverRect(HDC dc, const Game& gm, const RECT& r) {
  if (!gm.cover) {
    ui::Fill(dc, r, kWhite);
    std::wstring ch(1, gm.title.empty() ? L'?' : gm.title[0]);
    ui::Text(dc, ch, r, g.fBig, RGB(0x90, 0x90, 0x90), CENTER);
    return;
  }
  HDC m = CreateCompatibleDC(dc);
  HGDIOBJ o = SelectObject(m, gm.cover);
  BITMAP b; GetObject(gm.cover, sizeof(b), &b);
  int dw = r.right - r.left, dh = r.bottom - r.top;
  int sx = 0, sy = 0, sw = b.bmWidth, sh = b.bmHeight;
  double da = (double)dw / dh, sa = (double)b.bmWidth / b.bmHeight;
  if (sa > da) { sw = (int)(b.bmHeight * da); sx = (b.bmWidth - sw) / 2; }
  else { sh = (int)(b.bmWidth / da); sy = (b.bmHeight - sh) / 2; }
  SetStretchBltMode(dc, HALFTONE);
  SetBrushOrgEx(dc, 0, 0, NULL);
  StretchBlt(dc, r.left, r.top, dw, dh, m, sx, sy, sw, sh, SRCCOPY);
  SelectObject(m, o);
  DeleteDC(m);
}

static void DrawGrid(HDC dc, const Layout& lo, const std::vector<Game>& v) {
  int saved = SaveDC(dc);
  IntersectClipRect(dc, lo.gridArea.left, lo.gridArea.top, lo.gridArea.right, lo.gridArea.bottom);
  for (size_t k = 0; k < lo.tiles.size(); ++k) {
    const RECT& t = lo.tiles[k];
    int idx = lo.tileIdx[k];
    const Game& gm = v[idx];
    bool hv = g.hover == H_TILE + idx;
    ui::Fill(dc, t, kSurface);
    DrawCoverRect(dc, gm, R(t.left, t.top, t.right, t.top + TILE_IMG_H));
    if (hv) ui::Frame(dc, t, kAccent, 2);
    ui::Text(dc, gm.title, R(t.left + 6, t.top + TILE_IMG_H, t.right - 6, t.bottom), g.fSmall,
             hv ? kWhite : kText, LEFTV | DT_END_ELLIPSIS);
  }
  RestoreDC(dc, saved);
}

static void DrawCenterNote(HDC dc, const RECT& area, const std::wstring& title, const std::wstring& sub) {
  RECT t = area; t.bottom = (area.top + area.bottom) / 2;
  ui::Text(dc, title, t, g.fBig, kWhite, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
  RECT s = area; s.top = t.bottom + 8; s.left += 30; s.right -= 30;
  ui::Text(dc, sub, s, g.fSmall, kDim, DT_CENTER | DT_TOP | DT_WORDBREAK);
}

static void DrawLibrary(HDC dc, const Layout& lo) {
  ui::Fill(dc, lo.side, kSurface);
  ui::Fill(dc, R(lo.side.right - 1, lo.side.top, lo.side.right, lo.side.bottom), kDarkest);
  ui::Fill(dc, lo.searchBox, kSteel);
  std::vector<int> f = FilterLib();
  int y = lo.searchBox.bottom + 12;
  ui::Text(dc, L"ALL (" + NumToW((unsigned long)f.size()) + L")", R(14, y, SIDEBAR_W - 10, y + 22), g.fSmallB, kDim, LEFTV);

  int saved = SaveDC(dc);
  IntersectClipRect(dc, lo.sideList.left, lo.sideList.top, lo.sideList.right, lo.sideList.bottom);
  for (size_t k = 0; k < lo.sideRows.size(); ++k) {
    int idx = lo.sideIdx[k];
    bool hv = g.hover == H_SIDE + idx;
    if (hv) ui::Fill(dc, lo.sideRows[k], kSteel);
    RECT tr = lo.sideRows[k]; tr.left = 14; tr.right -= 8;
    ui::Text(dc, g.lib[idx].title, tr, g.fSmall, hv ? kWhite : kText, LEFTV | DT_END_ELLIPSIS);
  }
  RestoreDC(dc, saved);

  ui::VGradient(dc, lo.mainArea, kBase, kSurface);
  ui::Text(dc, L"GAMES", R(SIDEBAR_W + 24, lo.body.top, lo.mainArea.right, lo.body.top + 36), g.fSmallB, kDim, LEFTV);
  if (!lo.tiles.empty()) { DrawGrid(dc, lo, g.lib); return; }
  if (g.libLoading) DrawCenterNote(dc, lo.gridArea, L"Loading your library...", g.status);
  else if (!g.libErr.empty()) DrawCenterNote(dc, lo.gridArea, L"Could not load library", g.libErr);
  else if (g.lib.empty()) DrawCenterNote(dc, lo.gridArea, L"Your library is empty",
      L"Games you own or claim on itch.io appear here.");
  else DrawCenterNote(dc, lo.gridArea, L"No games match your search", L"");
}

static void DrawStore(HDC dc, const Layout& lo) {
  ui::VGradient(dc, lo.body, kBase, kSurface);
  ui::Fill(dc, lo.searchBox, kSteel);
  ui::GreenButton(dc, lo.btnSearch, L"SEARCH", g.fSmallB, g.hover == H_SEARCHBTN);
  if (!lo.tiles.empty()) { DrawGrid(dc, lo, g.store); return; }
  if (g.storeLoading) DrawCenterNote(dc, lo.gridArea, L"Searching itch.io...", L"");
  else if (!g.storeErr.empty()) DrawCenterNote(dc, lo.gridArea, L"Search failed", g.storeErr);
  else DrawCenterNote(dc, lo.gridArea, L"No results", L"Try another search");
}

static void DrawReqLine(HDC dc, int x, int y, int w, const wchar_t* label, const wchar_t* val) {
  ui::Text(dc, label, R(x, y, x + 110, y + 20), g.fSmallB, kDim, LEFTV);
  ui::Text(dc, val, R(x + 112, y, x + w, y + 20), g.fSmall, kText, LEFTV | DT_END_ELLIPSIS);
}

static void DrawGamePage(HDC dc, const Layout& lo) {
  ui::Fill(dc, lo.body, kBase);
  Game* gm = CurGame();
  if (!gm) return;
  int w = lo.body.right, bt = lo.body.top;

  // Банер-шапка в стилі сторінки гри Steam: обкладинка на всю ширину, затемнена знизу, з назвою поверх
  int saved = SaveDC(dc);
  IntersectClipRect(dc, lo.hero.left, lo.hero.top, lo.hero.right, lo.hero.bottom);
  RECT wide = R(lo.hero.left - 200, lo.hero.top, lo.hero.right + 200, lo.hero.bottom);
  DrawCoverRect(dc, *gm, wide);
  RECT shade = R(lo.hero.left, lo.hero.bottom - 110, lo.hero.right, lo.hero.bottom);
  ui::VGradient(dc, shade, kSurface, kDarkest);  // затемнення знизу, щоб читався заголовок
  RestoreDC(dc, saved);
  ui::Fill(dc, R(lo.hero.left, lo.hero.bottom - 4, lo.hero.right, lo.hero.bottom), kDarkest);
  ui::Text(dc, gm->title, R(40, lo.hero.bottom - 58, w - 40, lo.hero.bottom - 14), g.fBig, kWhite, LEFTV | DT_END_ELLIPSIS);

  int colW = lo.reqBox.left - 40 - 20;
  if (colW < 100) colW = w - 80;
  int descTop = lo.hero.bottom + 24;
  std::wstring desc = gm->local ? gm->path : gm->shortText;
  if (desc.empty()) desc = L"No description provided.";
  ui::Text(dc, desc, R(40, descTop, 40 + colW, descTop + 68), g.fSmall, kText, DT_LEFT | DT_TOP | DT_WORDBREAK);

  ui::GreenButton(dc, lo.btnInstall, ActionLabel(*gm), g.fNav, g.hover == H_INSTALL);
  if (!gm->local) {
    bool hv = g.hover == H_OPENWEB;
    ui::VGradient(dc, lo.btnWeb, hv ? RGB(0x4a, 0x6e, 0x8c) : kSteel, hv ? RGB(0x2f, 0x4d, 0x66) : RGB(0x1f, 0x36, 0x4a));
    ui::Text(dc, L"OPEN ON ITCH.IO", lo.btnWeb, g.fSmallB, kWhite, CENTER);
  }

  int msgTop = lo.btnInstall.bottom + 18;
  if (g.dl.busy && g.dl.gameId == gm->id) {
    ui::Text(dc, g.dl.extracting ? L"Unpacking..." : g.dl.status, R(40, msgTop, 40 + colW, msgTop + 22), g.fSmall, kText, LEFTV);
    ui::ProgressBar(dc, R(40, msgTop + 28, 40 + colW, msgTop + 38), g.dl.pct / 100.0);
  } else if (!g.gameMsg.empty()) {
    ui::Text(dc, g.gameMsg, R(40, msgTop, 40 + colW, msgTop + 56), g.fSmall, kDim, DT_LEFT | DT_TOP | DT_WORDBREAK);
  }

  // Системні вимоги. itch.io не передає характеристики гри через API, тож тут орієнтовні
  // вимоги самого лаунчера (підтвердженого Windows 7), а не точний спек конкретної гри.
  const RECT& rb = lo.reqBox;
  ui::Fill(dc, rb, kSurface);
  ui::Frame(dc, rb, kDarkest, 1);
  ui::Text(dc, L"SYSTEM REQUIREMENTS", R(rb.left + 16, rb.top + 12, rb.right - 16, rb.top + 32), g.fSmallB, kWhite, LEFTV);
  ui::Text(dc, L"MINIMUM:", R(rb.left + 16, rb.top + 40, rb.right - 16, rb.top + 58), g.fSmallB, kAccent, LEFTV);
  int ry = rb.top + 64, rw = rb.right - rb.left - 32;
  DrawReqLine(dc, rb.left + 16, ry, rw, L"OS:", L"Windows 7 SP1 or later"); ry += 24;
  DrawReqLine(dc, rb.left + 16, ry, rw, L"Processor:", L"1.6 GHz dual-core"); ry += 24;
  DrawReqLine(dc, rb.left + 16, ry, rw, L"Memory:", L"2 GB RAM"); ry += 24;
  DrawReqLine(dc, rb.left + 16, ry, rw, L"Graphics:", L"DirectX 9 compatible"); ry += 24;
  DrawReqLine(dc, rb.left + 16, ry, rw, L"Storage:", L"Varies by game"); ry += 32;
  ui::Text(dc, L"Approximate; itch.io does not provide per-game specs.",
           R(rb.left + 16, ry, rb.right - 16, rb.bottom - 10), g.fSmall, kDim, DT_LEFT | DT_TOP | DT_WORDBREAK);
}

static void DrawDownloads(HDC dc, const Layout& lo) {
  ui::VGradient(dc, lo.body, kBase, kSurface);
  int w = lo.body.right, bt = lo.body.top;
  ui::Text(dc, L"DOWNLOADS", R(40, bt + 26, w - 40, bt + 66), g.fBig, kWhite, LEFTV);
  if (g.dl.title.empty()) {
    ui::Text(dc, L"Nothing in the queue", R(40, bt + 80, w - 40, bt + 104), g.fSmall, kDim, LEFTV);
    return;
  }
  RECT row = R(40, bt + 84, w - 40, bt + 84 + 90);
  ui::Fill(dc, row, kSurface);
  RECT coverR = R(row.left + 10, row.top + 10, row.left + 10 + 120, row.bottom - 10);
  Game* gm = GameById(g.dl.gameId);
  if (gm) DrawCoverRect(dc, *gm, coverR); else ui::Fill(dc, coverR, kSteel);

  int tx = coverR.right + 20;
  ui::Text(dc, g.dl.title, R(tx, row.top + 14, row.right - 16, row.top + 40), g.fMid, kWhite, LEFTV | DT_END_ELLIPSIS);
  std::wstring state = g.dl.extracting ? L"Unpacking..." : (g.dl.busy ? g.dl.status : g.dl.status);
  ui::Text(dc, state, R(tx, row.top + 44, row.right - 16, row.top + 64), g.fSmall, kDim, LEFTV | DT_END_ELLIPSIS);
  if (g.dl.extracting)
    ui::ProgressBar(dc, R(tx, row.top + 70, row.right - 16, row.top + 80), 1.0);
  else
    ui::ProgressBar(dc, R(tx, row.top + 70, row.right - 16, row.top + 80), g.dl.pct / 100.0);
}

static void DrawCommunity(HDC dc, const Layout& lo) {
  ui::VGradient(dc, lo.body, kBase, kSurface);
  ui::Text(dc, L"COMMUNITY", R(40, lo.body.top + 26, lo.body.right - 40, lo.body.top + 66), g.fBig, kWhite, LEFTV);
  static const wchar_t* titles[3] = { L"Community", L"Game Jams", L"Devlogs" };
  static const wchar_t* subs[3] = { L"Forums and discussions on itch.io", L"Browse game jams", L"Latest development logs" };
  for (int i = 0; i < 3; i++) {
    bool hv = g.hover == H_CARD0 + i;
    ui::Fill(dc, lo.cards[i], hv ? kSteel : kSurface);
    ui::Text(dc, titles[i], R(lo.cards[i].left + 16, lo.cards[i].top + 16, lo.cards[i].right - 16, lo.cards[i].top + 50), g.fMid, kWhite, LEFTV);
    ui::Text(dc, subs[i], R(lo.cards[i].left + 16, lo.cards[i].top + 56, lo.cards[i].right - 16, lo.cards[i].bottom - 10), g.fSmall, kDim, DT_LEFT | DT_TOP | DT_WORDBREAK);
  }
}

static void DrawProfile(HDC dc, const Layout& lo) {
  ui::VGradient(dc, lo.body, kBase, kSurface);
  ui::Text(dc, g.username.empty() ? L"OFFLINE" : g.username, R(40, lo.body.top + 26, lo.body.right - 40, lo.body.top + 66), g.fBig, kWhite, LEFTV);
  ui::Text(dc, g.status, R(40, lo.body.top + 76, lo.body.right - 40, lo.body.top + 100), g.fSmall, kDim, LEFTV);
  bool h1 = g.hover == H_OPENPROFILE, h2 = g.hover == H_SIGNOUT;
  ui::GreenButton(dc, lo.btnProfile, L"OPEN ITCH.IO PROFILE", g.fSmallB, h1);
  ui::VGradient(dc, lo.btnSignOut, h2 ? RGB(0x4a, 0x6e, 0x8c) : kSteel, h2 ? RGB(0x2f, 0x4d, 0x66) : RGB(0x1f, 0x36, 0x4a));
  ui::Text(dc, L"SIGN OUT", lo.btnSignOut, g.fSmallB, kWhite, CENTER);
}

static void DrawAccountPage(HDC dc, const Layout& lo, const RECT& full) {
  ui::VGradient(dc, full, kDarkest, kBase);
  ui::Fill(dc, lo.accBox, kDeep);
  ui::Frame(dc, lo.accBox, kSteel, 1);
  const RECT& b = lo.accBox;
  ui::Text(dc, L"STEAM WIN7", R(b.left, b.top + 24, b.right, b.top + 56), g.fBig, kAccent, CENTER);
  ui::Text(dc, L"Create a new Steam Win7 account", R(b.left + 20, b.top + 70, b.right - 20, b.top + 100), g.fMid, kWhite, CENTER);
  ui::Text(dc, L"Local account for this launcher only. Nothing is sent anywhere.",
           R(b.left + 30, b.top + 102, b.right - 30, b.top + 132), g.fSmall, kDim, DT_CENTER | DT_TOP | DT_WORDBREAK);

  ui::Text(dc, L"USERNAME", R(b.left + 30, b.top + 122, b.right - 30, b.top + 140), g.fSmall, kDim, LEFTV);
  ui::Fill(dc, lo.accUserBox, kSteel);
  ui::Text(dc, L"PASSWORD", R(b.left + 30, b.top + 178, b.right - 30, b.top + 196), g.fSmall, kDim, LEFTV);
  ui::Fill(dc, lo.accPassBox, kSteel);
  ui::Text(dc, L"CONFIRM PASSWORD", R(b.left + 30, b.top + 234, b.right - 30, b.top + 252), g.fSmall, kDim, LEFTV);
  ui::Fill(dc, lo.accPass2Box, kSteel);

  if (!g.accErr.empty())
    ui::Text(dc, g.accErr, R(b.left + 30, b.top + 292, b.right - 30, b.top + 318), g.fSmall, RGB(0xe0, 0x6c, 0x6c), LEFTV | DT_WORDBREAK);

  ui::GreenButton(dc, lo.btnCreateAcc, L"CREATE ACCOUNT", g.fNav, g.hover == H_CREATEACC);
}

static void DrawLogin(HDC dc, const Layout& lo) {
  ui::VGradient(dc, lo.body, kBase, kDarkest);
  ui::Fill(dc, lo.loginBox, kDeep);
  ui::Frame(dc, lo.loginBox, kDarkest, 1);
  const RECT& b = lo.loginBox;
  ui::Text(dc, L"Sign in to itch.io", R(b.left + 30, b.top + 24, b.right - 30, b.top + 70), g.fBig, kWhite, LEFTV);
  ui::Text(dc, L"Paste your API key (itch.io > Settings > API keys)", R(b.left + 30, b.top + 84, b.right - 30, b.top + 124), g.fSmall, kDim, DT_LEFT | DT_TOP | DT_WORDBREAK);
  ui::Fill(dc, lo.keyBox, kSteel);
  if (!g.loginErr.empty())
    ui::Text(dc, g.loginErr, R(b.left + 30, b.top + 176, b.right - 30, b.top + 214), g.fSmall, RGB(0xe0, 0x6c, 0x6c), DT_LEFT | DT_TOP | DT_WORDBREAK);
  ui::GreenButton(dc, lo.btnSignIn, L"SIGN IN", g.fNav, g.hover == H_SIGNIN);
}

static void DrawBottom(HDC dc, const Layout& lo) {
  ui::Fill(dc, lo.bottom, kDeep);
  ui::Fill(dc, R(0, lo.bottom.top, lo.bottom.right, lo.bottom.top + 1), kDarkest);
  std::wstring dlText = L"DOWNLOADS";
  if (g.dl.extracting) dlText = L"UNPACKING...";
  else if (g.dl.busy) dlText = L"DOWNLOADING  " + NumToW((unsigned long)g.dl.pct) + L"%";
  ui::Text(dc, dlText, lo.dl, g.fSmallB, g.hover == H_DL ? kWhite : kText, CENTER);
  if (g.dl.busy)
    ui::ProgressBar(dc, R(lo.dl.left + 20, lo.bottom.bottom - 6, lo.dl.right - 20, lo.bottom.bottom - 3), g.dl.pct / 100.0);
  ui::Text(dc, L"FRIENDS & CHAT", lo.friends, g.fSmallB, g.hover == H_FRIENDS ? kWhite : kText, CENTER);
}

static void Paint(HDC dc, const RECT& c) {
  Layout lo = Compute(dc, c);
  SetBkMode(dc, TRANSPARENT);

  if (g.page == PAGE_ACCOUNT) { DrawAccountPage(dc, lo, c); return; }

  switch (g.page) {
    case PAGE_LIBRARY:   DrawLibrary(dc, lo); break;
    case PAGE_STORE:     DrawStore(dc, lo); break;
    case PAGE_GAME:      DrawGamePage(dc, lo); break;
    case PAGE_DOWNLOADS: DrawDownloads(dc, lo); break;
    case PAGE_COMMUNITY: DrawCommunity(dc, lo); break;
    case PAGE_PROFILE:   DrawProfile(dc, lo); break;
    default:             DrawLogin(dc, lo); break;
  }
  DrawBottom(dc, lo);

  ui::Fill(dc, lo.menu, kDeep);
  for (int i = 0; i < 5; i++) {
    bool hv = g.hover == H_MENU0 + i;
    if (hv) ui::Fill(dc, lo.menuItem[i], kSteel);
    ui::Text(dc, kMenus[i], lo.menuItem[i], g.fMenu, hv ? kWhite : kText, CENTER);
  }

  ui::Fill(dc, lo.nav, kDeep);
  ui::Fill(dc, R(0, lo.nav.bottom - 1, c.right, lo.nav.bottom), kDarkest);
  ui::Text(dc, L"\u2190", lo.back, g.fNav, g.page == PAGE_GAME ? (g.hover == H_BACK ? kWhite : kText) : kDim, CENTER);
  ui::Text(dc, L"\u2192", lo.fwd, g.fNav, kDim, CENTER);
  int activeTab = (g.page == PAGE_GAME) ? (int)g.prevPage : (int)g.page;
  for (int i = 0; i < 4; i++) {
    bool act = activeTab == i, hv = g.hover == H_TAB0 + i;
    ui::Text(dc, TabLabel(i), lo.tab[i], g.fNav, (act || hv) ? kWhite : kText, CENTER);
    if (act) ui::Fill(dc, R(lo.tab[i].left + 10, lo.nav.bottom - 3, lo.tab[i].right - 10, lo.nav.bottom), kAccent);
  }
}

// ---------- логіка ----------

static void PositionControls(HWND hwnd) {
  Layout lo = CurLayout(hwnd);
  bool lib = g.page == PAGE_LIBRARY, st = g.page == PAGE_STORE, lg = g.page == PAGE_LOGIN, ac = g.page == PAGE_ACCOUNT;
  ShowWindow(g.search, (lib || st) ? SW_SHOW : SW_HIDE);
  ShowWindow(g.keyEdit, lg ? SW_SHOW : SW_HIDE);
  ShowWindow(g.accUser, ac ? SW_SHOW : SW_HIDE);
  ShowWindow(g.accPass, ac ? SW_SHOW : SW_HIDE);
  ShowWindow(g.accPass2, ac ? SW_SHOW : SW_HIDE);
  if (lib) MoveWindow(g.search, lo.searchBox.left + 8, lo.searchBox.top + 5, lo.searchBox.right - lo.searchBox.left - 16, 18, TRUE);
  if (st)  MoveWindow(g.search, lo.searchBox.left + 10, lo.searchBox.top + 7, lo.searchBox.right - lo.searchBox.left - 20, 18, TRUE);
  if (lg)  MoveWindow(g.keyEdit, lo.keyBox.left + 8, lo.keyBox.top + 8, lo.keyBox.right - lo.keyBox.left - 16, 18, TRUE);
  if (ac) {
    MoveWindow(g.accUser, lo.accUserBox.left + 8, lo.accUserBox.top + 8, lo.accUserBox.right - lo.accUserBox.left - 16, 18, TRUE);
    MoveWindow(g.accPass, lo.accPassBox.left + 8, lo.accPassBox.top + 8, lo.accPassBox.right - lo.accPassBox.left - 16, 18, TRUE);
    MoveWindow(g.accPass2, lo.accPass2Box.left + 8, lo.accPass2Box.top + 8, lo.accPass2Box.right - lo.accPass2Box.left - 16, 18, TRUE);
  }
}

static void SetPage(HWND hwnd, Page p) {
  if (g.apiKey.empty() && p != PAGE_COMMUNITY && p != PAGE_LOGIN) p = PAGE_LOGIN;
  Page old = g.page;
  Page oldEff = (old == PAGE_GAME) ? g.prevPage : old;
  if (p == PAGE_GAME) { g.prevPage = old; g.savedScroll = g.scroll; g.scroll = 0; }
  else if (old == PAGE_GAME && p == g.prevPage) g.scroll = g.savedScroll;
  else g.scroll = 0;
  g.page = p;
  if ((p == PAGE_LIBRARY || p == PAGE_STORE) && p != oldEff) {
    SetWindowTextW(g.search, L"");
    SendMessageW(g.search, EM_SETCUEBANNER, TRUE, (LPARAM)(p == PAGE_STORE ? L"Search itch.io" : L"Search"));
  }
  if (p == PAGE_STORE && !g.storeLoaded) StartSearch(L"game");
  PositionControls(hwnd);
  Redraw();
}

static void OpenMiniWindow(HWND owner, int src, long long id);

static void OpenGame(HWND hwnd, int src, int idx) {
  g.curSrc = src; g.curIdx = idx; g.gameMsg.clear();
  SetPage(hwnd, PAGE_GAME);
}

static void OpenUrl(HWND hwnd, const std::wstring& url) {
  if (!url.empty()) ShellExecuteW(hwnd, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

static void SaveLocal() {
  std::vector<std::wstring> v;
  for (size_t i = 0; i < g.lib.size(); ++i) if (g.lib[i].local) v.push_back(g.lib[i].path);
  SaveLocalGames(v);
}

static void AddLocalPath(const std::wstring& path) {
  Game gm;
  gm.local = true;
  long long n = 0;
  for (size_t i = 0; i < g.lib.size(); ++i) if (g.lib[i].local) ++n;
  gm.id = -(n + 1);
  gm.path = path;
  size_t s = path.find_last_of(L"\\/");
  std::wstring name = s == std::wstring::npos ? path : path.substr(s + 1);
  size_t d = name.find_last_of(L'.');
  if (d != std::wstring::npos && d > 0) name = name.substr(0, d);
  gm.title = name;
  g.lib.push_back(gm);
}

static void AddLocalGame(HWND hwnd) {
  wchar_t file[MAX_PATH] = L"";
  OPENFILENAMEW ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd;
  ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All files\0*.*\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = L"Add a Game";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn)) return;
  AddLocalPath(file);
  SaveLocal();
  SetPage(hwnd, PAGE_LIBRARY);
}

static void DoCreateAccount(HWND hwnd) {
  wchar_t u[256] = {0}, p1[256] = {0}, p2[256] = {0};
  GetWindowTextW(g.accUser, u, 256);
  GetWindowTextW(g.accPass, p1, 256);
  GetWindowTextW(g.accPass2, p2, 256);
  std::wstring user(u), pass(p1), pass2(p2);
  if (user.empty())      { g.accErr = L"Enter a username"; Redraw(); return; }
  if (pass.size() < 4)   { g.accErr = L"Password must be at least 4 characters"; Redraw(); return; }
  if (pass != pass2)     { g.accErr = L"Passwords do not match"; Redraw(); return; }
  CreateAccount(user, pass);
  SetWindowTextW(g.accUser, L""); SetWindowTextW(g.accPass, L""); SetWindowTextW(g.accPass2, L"");
  g.accErr.clear();
  g.apiKey = LoadApiKey();
  SetPage(hwnd, g.apiKey.empty() ? PAGE_LOGIN : PAGE_LIBRARY);
  if (!g.apiKey.empty()) StartLoader();
}

static void DoSignIn(HWND hwnd) {
  wchar_t b[512] = {0};
  GetWindowTextW(g.keyEdit, b, 512);
  std::string k;
  for (wchar_t* p = b; *p; ++p) if (*p > 32 && *p < 127) k += (char)*p;
  if (k.empty()) { g.loginErr = L"Enter your itch.io API key"; Redraw(); return; }
  g.apiKey = k; g.loginErr.clear();
  SaveApiKey(k);
  SetWindowTextW(g.keyEdit, L"");
  FreeGames(g.lib); FreeGames(g.store);
  g.username.clear(); g.storeLoaded = false; g.curIdx = -1;
  StartLoader();
  SetPage(hwnd, PAGE_LIBRARY);
}

static void DoSignOut(HWND hwnd) {
  g.apiKey.clear();
  SaveApiKey("");
  FreeGames(g.lib); FreeGames(g.store);
  g.username.clear(); g.profileUrl.clear(); g.storeLoaded = false; g.curIdx = -1; g.loginErr.clear();
  SetPage(hwnd, PAGE_LOGIN);
}

static void DoActionFor(HWND hwnd, Game* gm) {
  if (!gm) return;
  if (gm->path.empty()) { StartInstall(*gm); return; }
  if (IsExe(gm->path)) ShellExecuteW(hwnd, L"open", gm->path.c_str(), NULL, DirOf(gm->path).c_str(), SW_SHOWNORMAL);
  else ShellExecuteW(hwnd, L"open", DirOf(gm->path).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

static void DoAction(HWND hwnd) { DoActionFor(hwnd, CurGame()); }

static int FindIdx(int src, long long id) {
  std::vector<Game>& v = src ? g.store : g.lib;
  for (size_t i = 0; i < v.size(); ++i) if (v[i].id == id) return (int)i;
  return -1;
}

static void DoStoreSearch() {
  wchar_t b[256] = {0};
  GetWindowTextW(g.search, b, 256);
  std::wstring q(b);
  if (q.empty()) q = L"game";
  g.scroll = 0;
  StartSearch(q);
}

static void ShowMenu(HWND hwnd, int which) {
  Layout lo = CurLayout(hwnd);
  HMENU m = CreatePopupMenu();
  if (which == 0) { AppendMenuW(m, MF_STRING, 2, L"Sign out"); AppendMenuW(m, MF_STRING, 1, L"Exit"); }
  else if (which == 1) {
    AppendMenuW(m, MF_STRING, 11, L"Library"); AppendMenuW(m, MF_STRING, 12, L"Store");
    AppendMenuW(m, MF_STRING, 13, L"Downloads"); AppendMenuW(m, MF_STRING, 14, L"Community");
  }
  else if (which == 2) AppendMenuW(m, MF_STRING, 41, L"Open itch.io community");
  else if (which == 3) AppendMenuW(m, MF_STRING, 21, L"Add a Non-Steam Game to My Library...");
  else AppendMenuW(m, MF_STRING, 31, L"About");
  POINT p = { lo.menuItem[which].left, lo.menuItem[which].bottom };
  ClientToScreen(hwnd, &p);
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, hwnd, NULL);
  DestroyMenu(m);
  switch (cmd) {
    case 1: DestroyWindow(hwnd); break;
    case 2: DoSignOut(hwnd); break;
    case 11: SetPage(hwnd, PAGE_LIBRARY); break;
    case 12: SetPage(hwnd, PAGE_STORE); break;
    case 13: SetPage(hwnd, PAGE_DOWNLOADS); break;
    case 14: SetPage(hwnd, PAGE_COMMUNITY); break;
    case 21: AddLocalGame(hwnd); break;
    case 31: MessageBoxW(hwnd, L"Steam (Windows 7 edition)\nPrivate itch.io launcher", L"About", MB_OK | MB_ICONINFORMATION); break;
    case 41: OpenUrl(hwnd, L"https://itch.io/community"); break;
  }
}

static void OnClick(HWND hwnd, int h) {
  if (h == H_NONE) return;
  if (h == H_BACK) { if (g.page == PAGE_GAME) SetPage(hwnd, g.prevPage); return; }
  if (h >= H_TAB0 && h < H_TAB0 + 4) { SetPage(hwnd, (Page)(h - H_TAB0)); return; }
  if (h >= H_MENU0 && h < H_MENU0 + 5) { ShowMenu(hwnd, h - H_MENU0); return; }
  if (h >= H_CARD0 && h < H_CARD0 + 3) {
    static const wchar_t* urls[3] = { L"https://itch.io/community", L"https://itch.io/jams", L"https://itch.io/devlogs" };
    OpenUrl(hwnd, urls[h - H_CARD0]);
    return;
  }
  if (h >= H_TILE) {
    int src = g.page == PAGE_STORE ? 1 : 0, idx = h - H_TILE;
    std::vector<Game>& v = src ? g.store : g.lib;
    if (idx >= 0 && idx < (int)v.size()) OpenMiniWindow(hwnd, src, v[idx].id);
    return;
  }
  if (h >= H_SIDE) {
    int idx = h - H_SIDE;
    if (idx >= 0 && idx < (int)g.lib.size()) OpenMiniWindow(hwnd, 0, g.lib[idx].id);
    return;
  }
  switch (h) {
    case H_DL: SetPage(hwnd, PAGE_DOWNLOADS); break;
    case H_FRIENDS: OpenUrl(hwnd, L"https://itch.io/community"); break;
    case H_SIGNIN: DoSignIn(hwnd); break;
    case H_CREATEACC: DoCreateAccount(hwnd); break;
    case H_SIGNOUT: DoSignOut(hwnd); break;
    case H_OPENPROFILE: OpenUrl(hwnd, g.profileUrl.empty() ? L"https://itch.io" : g.profileUrl); break;
    case H_SEARCHBTN: DoStoreSearch(); break;
    case H_INSTALL: DoAction(hwnd); break;
    case H_OPENWEB: { Game* gm = CurGame(); if (gm) OpenUrl(hwnd, gm->url); break; }
  }
}

static WNDPROC g_oldEdit = NULL;
static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_CHAR && w == VK_RETURN) { PostMessageW(g.hwnd, WM_ENTER, 0, 0); return 0; }
  return CallWindowProcW(g_oldEdit, h, m, w, l);
}

static bool FileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static bool GameLess(const Game& a, const Game& b) { return Lower(a.title) < Lower(b.title); }

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g.hwnd = hwnd;
      g.fMenu = ui::MakeFont(13, false); g.fNav = ui::MakeFont(15, true);
      g.fSmall = ui::MakeFont(13, false); g.fSmallB = ui::MakeFont(12, true);
      g.fBig = ui::MakeFont(26, false); g.fMid = ui::MakeFont(18, false);
      g.hover = H_NONE; g.tracking = false; g.curIdx = -1; g.prevPage = PAGE_LIBRARY;

      HINSTANCE hi = GetModuleHandleW(NULL);
      g.search  = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 100, 18, hwnd, NULL, hi, NULL);
      g.keyEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 100, 18, hwnd, NULL, hi, NULL);
      g.accUser  = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 100, 18, hwnd, NULL, hi, NULL);
      g.accPass  = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD, 0, 0, 100, 18, hwnd, NULL, hi, NULL);
      g.accPass2 = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD, 0, 0, 100, 18, hwnd, NULL, hi, NULL);
      HWND ctl[5] = { g.search, g.keyEdit, g.accUser, g.accPass, g.accPass2 };
      for (int i = 0; i < 5; ++i) SendMessageW(ctl[i], WM_SETFONT, (WPARAM)g.fSmall, TRUE);
      SendMessageW(g.search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search");
      SendMessageW(g.keyEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"API key");
      SendMessageW(g.accUser, EM_SETCUEBANNER, TRUE, (LPARAM)L"Username");
      SendMessageW(g.accPass, EM_SETCUEBANNER, TRUE, (LPARAM)L"Password");
      SendMessageW(g.accPass2, EM_SETCUEBANNER, TRUE, (LPARAM)L"Confirm password");
      g_oldEdit = (WNDPROC)SetWindowLongPtrW(g.search, GWLP_WNDPROC, (LONG_PTR)EditProc);
      SetWindowLongPtrW(g.keyEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);
      SetWindowLongPtrW(g.accUser, GWLP_WNDPROC, (LONG_PTR)EditProc);
      SetWindowLongPtrW(g.accPass, GWLP_WNDPROC, (LONG_PTR)EditProc);
      SetWindowLongPtrW(g.accPass2, GWLP_WNDPROC, (LONG_PTR)EditProc);

      if (!HasAccount()) {
        g.page = PAGE_ACCOUNT;
      } else {
        g.apiKey = LoadApiKey();
        g.page = g.apiKey.empty() ? PAGE_LOGIN : PAGE_LIBRARY;
        if (!g.apiKey.empty()) StartLoader(); else g.status = L"Offline";
      }
      PositionControls(hwnd);
      return 0;
    }
    case WM_SIZE: PositionControls(hwnd); Redraw(); return 0;
    case WM_GETMINMAXINFO: {
      MINMAXINFO* mm = (MINMAXINFO*)lp;
      mm->ptMinTrackSize.x = 900; mm->ptMinTrackSize.y = 600;
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT c; GetClientRect(hwnd, &c);
      HDC mem = CreateCompatibleDC(dc);
      HBITMAP bmp = CreateCompatibleBitmap(dc, c.right, c.bottom);
      HGDIOBJ old = SelectObject(mem, bmp);
      Paint(mem, c);
      BitBlt(dc, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteObject(bmp);
      DeleteDC(mem);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_CTLCOLOREDIT: {
      HDC d = (HDC)wp;
      static HBRUSH br = CreateSolidBrush(kSteel);
      SetTextColor(d, kText);
      SetBkColor(d, kSteel);
      return (LRESULT)br;
    }
    case WM_COMMAND:
      if (HIWORD(wp) == EN_CHANGE && (HWND)lp == g.search) { g.scroll = 0; g.sideScroll = 0; Redraw(); }
      return 0;
    case WM_ENTER:
      if (g.page == PAGE_STORE) DoStoreSearch();
      else if (g.page == PAGE_LOGIN) DoSignIn(hwnd);
      else if (g.page == PAGE_ACCOUNT) DoCreateAccount(hwnd);
      return 0;
    case WM_MOUSEWHEEL: {
      int d = GET_WHEEL_DELTA_WPARAM(wp);
      POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
      ScreenToClient(hwnd, &p);
      if (g.page == PAGE_LIBRARY && p.x < SIDEBAR_W) g.sideScroll -= d / 2;
      else g.scroll -= d / 2;
      Redraw();
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (!g.tracking) {
        TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&t);
        g.tracking = true;
      }
      int h = HitTest(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (h != g.hover) { g.hover = h; Redraw(); }
      return 0;
    }
    case WM_MOUSELEAVE: g.tracking = false; g.hover = H_NONE; Redraw(); return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        SetCursor(LoadCursor(NULL, g.hover != H_NONE ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN: OnClick(hwnd, HitTest(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))); return 0;

    case WM_PROFILE: {
      std::wstring* s = (std::wstring*)lp;
      if (wp == 1) {
        size_t nl = s->find(L'\n');
        g.username = s->substr(0, nl);
        g.profileUrl = nl == std::wstring::npos ? L"" : s->substr(nl + 1);
        g.status = L"Connected";
      } else if (wp == 2) {
        g.apiKey.clear(); g.libLoading = false; g.loginErr = *s;
        SetPage(hwnd, PAGE_LOGIN);
      } else { g.status = *s; g.libLoading = false; g.libErr = *s; }
      delete s;
      Redraw();
      return 0;
    }
    case WM_LIBRARY: {
      std::vector<Game>* v = (std::vector<Game>*)lp;
      FreeGames(g.lib);
      g.lib = *v;
      delete v;
      std::sort(g.lib.begin(), g.lib.end(), GameLess);
      for (size_t i = 0; i < g.lib.size(); ++i) {
        std::wstring p = GetInstalledPath(g.lib[i].id);
        if (!p.empty() && FileExists(p)) g.lib[i].path = p;
      }
      ++g.gen[0];
      StartCovers(0, g.lib);
      std::vector<std::wstring> locals = LoadLocalGames();
      for (size_t i = 0; i < locals.size(); ++i) if (FileExists(locals[i])) AddLocalPath(locals[i]);
      g.libLoading = false;
      Redraw();
      return 0;
    }
    case WM_LIBERR: {
      std::wstring* s = (std::wstring*)lp;
      g.libLoading = false; g.libErr = *s;
      delete s;
      Redraw();
      return 0;
    }
    case WM_STORE: {
      unsigned seq = (unsigned)(wp >> 1);
      bool ok = (wp & 1) != 0;
      bool stale = seq != g.gen[1];
      if (ok) {
        std::vector<Game>* v = (std::vector<Game>*)lp;
        if (!stale) {
          FreeGames(g.store);
          g.store = *v;
          g.storeLoading = false;
          for (size_t i = 0; i < g.store.size(); ++i) {
            std::wstring p = GetInstalledPath(g.store[i].id);
            if (!p.empty() && FileExists(p)) g.store[i].path = p;
          }
          StartCovers(1, g.store);
        }
        delete v;
      } else {
        std::wstring* s = (std::wstring*)lp;
        if (!stale) { g.storeLoading = false; g.storeErr = *s; FreeGames(g.store); }
        delete s;
      }
      Redraw();
      return 0;
    }
    case WM_COVER: {
      unsigned gen = (unsigned)((wp >> 13) & 0x7FFF);
      int src = (int)((wp >> 12) & 1), idx = (int)(wp & 0xFFF);
      HBITMAP bm = (HBITMAP)lp;
      std::vector<Game>& v = src ? g.store : g.lib;
      if (gen != (g.gen[src] & 0x7FFF) || idx >= (int)v.size()) { DeleteObject(bm); return 0; }
      if (v[idx].cover) DeleteObject(v[idx].cover);
      v[idx].cover = bm;
      Redraw();
      return 0;
    }
    case WM_DLPROG: {
      if (wp == 101) {  // сентинел "розпакування"
        g.dl.extracting = true;
        g.dl.status = L"Unpacking...";
        Redraw();
        return 0;
      }
      unsigned long long* buf = (unsigned long long*)lp;
      g.dl.pct = (int)wp;
      g.dl.bytesDone = buf[0]; g.dl.bytesTotal = buf[1];
      DWORD now = GetTickCount();
      DWORD dt = now - g.dl.lastTick;
      if (dt >= 300 && g.dl.bytesDone >= g.dl.lastBytes) {
        g.dl.speedKBs = ((double)(g.dl.bytesDone - g.dl.lastBytes) / 1024.0) / (dt / 1000.0);
        g.dl.lastTick = now; g.dl.lastBytes = g.dl.bytesDone;
      }
      std::wstring mb = NumToW((unsigned long)(g.dl.bytesDone / 1024 / 1024));
      std::wstring mbTot = g.dl.bytesTotal ? (L" / " + NumToW((unsigned long)(g.dl.bytesTotal / 1024 / 1024)) + L" MB") : L" MB";
      std::wstring spd = g.dl.speedKBs > 1024 ? (NumToW((unsigned long)(g.dl.speedKBs / 1024)) + L".0 MB/s")
                                               : (NumToW((unsigned long)g.dl.speedKBs) + L" KB/s");
      std::wstring eta;
      if (g.dl.speedKBs > 1 && g.dl.bytesTotal > g.dl.bytesDone) {
        unsigned long secs = (unsigned long)(((g.dl.bytesTotal - g.dl.bytesDone) / 1024.0) / g.dl.speedKBs);
        eta = secs < 60 ? (NumToW(secs) + L"s left") : (NumToW(secs / 60) + L"m left");
      }
      g.dl.status = mb + mbTot + L"   " + spd + (eta.empty() ? L"" : (L"   " + eta));
      delete[] buf;
      Redraw();
      return 0;
    }
    case WM_DLDONE: {
      std::wstring* s = (std::wstring*)lp;
      g.dl.busy = false; g.dl.extracting = false;
      if (wp) {
        g.dl.pct = 100; g.dl.status = L"Ready to play";
        SetPathFor(g.dl.gameId, *s);
        Game* gm = CurGame();
        bool launchable = gm && gm->id == g.dl.gameId && IsExe(*s);
        g.gameMsg = launchable ? L"Installed. Click PLAY to launch the game."
                               : L"Downloaded to the games folder. Open the folder and run the game manually.";
      } else {
        g.dl.status = L"Failed: " + *s;
        g.gameMsg = g.dl.status;
      }
      delete s;
      Redraw();
      return 0;
    }
    case WM_DESTROY:
      if (g_miniWnd) DestroyWindow(g_miniWnd);
      FreeGames(g.lib); FreeGames(g.store);
      DeleteObject(g.fMenu); DeleteObject(g.fNav); DeleteObject(g.fSmall);
      DeleteObject(g.fSmallB); DeleteObject(g.fBig); DeleteObject(g.fMid);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------- міні-вікно гри (окреме вікно, не частина головного додатку) ----------

enum { MW_NONE = 0, MW_PLAY = 1, MW_MORE = 2 };
struct MiniData { long long id; int src; };

static RECT MiniCoverRect(const RECT& c) { return ui::R(16, 16, c.right - 16, c.right - 16 + (c.right - 32) * 3 / 4); }
static RECT MiniPlayRect(const RECT& c)  { RECT cv = MiniCoverRect(c); return ui::R(16, cv.bottom + 16, c.right - 16, cv.bottom + 58); }
static RECT MiniMoreRect(const RECT& c)  { RECT pr = MiniPlayRect(c); return ui::R(16, pr.bottom + 12, c.right - 16, pr.bottom + 34); }

static LRESULT CALLBACK MiniWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  MiniData* md = (MiniData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps; HDC dc0 = BeginPaint(hwnd, &ps);
      RECT c; GetClientRect(hwnd, &c);
      HDC dc = CreateCompatibleDC(dc0);
      HBITMAP bmp = CreateCompatibleBitmap(dc0, c.right, c.bottom);
      HGDIOBJ old = SelectObject(dc, bmp);
      SetBkMode(dc, TRANSPARENT);
      ui::Fill(dc, c, kDeep);
      Game* gm = md ? GameById(md->id) : NULL;
      RECT cv = MiniCoverRect(c);
      if (gm) DrawCoverRect(dc, *gm, cv); else ui::Fill(dc, cv, kWhite);
      ui::Frame(dc, cv, kDarkest, 1);
      ui::Text(dc, gm ? gm->title : L"", ui::R(16, cv.bottom + 2, c.right - 16, cv.bottom + 16), g.fSmallB, kWhite, LEFTV | DT_END_ELLIPSIS);
      RECT pr = MiniPlayRect(c);
      ui::GreenButton(dc, pr, gm ? ActionLabel(*gm) : L"PLAY", g.fNav, g_miniHover == MW_PLAY);
      RECT mr = MiniMoreRect(c);
      ui::Text(dc, L"More info \u2192", mr, g.fSmall, g_miniHover == MW_MORE ? kWhite : kAccent, CENTER);
      BitBlt(dc0, 0, 0, c.right, c.bottom, dc, 0, 0, SRCCOPY);
      SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(NULL, g_miniHover != MW_NONE ? IDC_HAND : IDC_ARROW)); return TRUE; }
      break;
    case WM_MOUSEMOVE: {
      RECT c; GetClientRect(hwnd, &c);
      int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
      int h = In(MiniPlayRect(c), x, y) ? MW_PLAY : (In(MiniMoreRect(c), x, y) ? MW_MORE : MW_NONE);
      if (h != g_miniHover) { g_miniHover = h; InvalidateRect(hwnd, NULL, FALSE); }
      return 0;
    }
    case WM_LBUTTONDOWN: {
      RECT c; GetClientRect(hwnd, &c);
      int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
      if (md && In(MiniPlayRect(c), x, y)) DoActionFor(hwnd, GameById(md->id));
      else if (md && In(MiniMoreRect(c), x, y)) {
        int idx = FindIdx(md->src, md->id);
        if (idx >= 0) { OpenGame(g.hwnd, md->src, idx); SetForegroundWindow(g.hwnd); }
        DestroyWindow(hwnd);
      }
      return 0;
    }
    case WM_NCDESTROY:
      delete md;
      if (g_miniWnd == hwnd) g_miniWnd = NULL;
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static void OpenMiniWindow(HWND owner, int src, long long id) {
  if (g_miniWnd) DestroyWindow(g_miniWnd);
  g_miniHover = MW_NONE;
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MiniWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"SteamGameMini";
    RegisterClassExW(&wc);
    registered = true;
  }
  Game* gm = GameById(id);
  RECT or_; GetWindowRect(owner, &or_);
  int w = 300, h = 400;
  HWND hwnd = CreateWindowExW(0, L"SteamGameMini", gm ? gm->title.c_str() : L"Game",
                              WS_POPUP | WS_CAPTION | WS_SYSMENU,
                              or_.left + 80, or_.top + 80, w, h, owner, NULL, GetModuleHandleW(NULL), NULL);
  MiniData* md = new MiniData; md->id = id; md->src = src;
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)md);
  g_miniWnd = hwnd;
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
  InitCommonControls();
  ImgInit();

  WNDCLASSEXW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
  wc.hIconSm = LoadIconW(hInst, MAKEINTRESOURCEW(1));
  wc.lpszClassName = L"SteamWin7Main";
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Steam", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720, NULL, NULL, hInst, NULL);
  ShowWindow(hwnd, nShow);
  UpdateWindow(hwnd);

  MSG m;
  while (GetMessageW(&m, NULL, 0, 0) > 0) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  ImgShutdown();
  return (int)m.wParam;
}
