#include <windows.h>
#include <string.h>
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>
#include "img.h"

static ULONG_PTR g_token = 0;

void ImgInit() {
  Gdiplus::GdiplusStartupInput in;
  Gdiplus::GdiplusStartup(&g_token, &in, NULL);
}

void ImgShutdown() {
  if (g_token) Gdiplus::GdiplusShutdown(g_token);
}

HBITMAP DecodeImage(const std::string& data) {
  if (data.empty()) return NULL;
  HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, data.size());
  if (!hg) return NULL;
  void* p = GlobalLock(hg);
  memcpy(p, data.data(), data.size());
  GlobalUnlock(hg);
  IStream* st = NULL;
  if (CreateStreamOnHGlobal(hg, TRUE, &st) != S_OK) { GlobalFree(hg); return NULL; }
  HBITMAP hb = NULL;
  {
    Gdiplus::Bitmap bmp(st);
    if (bmp.GetLastStatus() == Gdiplus::Ok)
      bmp.GetHBITMAP(Gdiplus::Color(0x16, 0x20, 0x2d), &hb);
  }
  st->Release();
  return hb;
}
