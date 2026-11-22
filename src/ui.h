#pragma once
#include <windows.h>
#include <string>
#include "theme.h"

namespace ui {
  HFONT MakeFont(int px, bool bold);
  void  Fill(HDC dc, const RECT& r, COLORREF c);
  void  Frame(HDC dc, const RECT& r, COLORREF c, int thick);
  void  VGradient(HDC dc, const RECT& r, COLORREF top, COLORREF bottom);
  void  Text(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fmt);
  void  ProgressBar(HDC dc, const RECT& r, double frac);
  void  GreenButton(HDC dc, const RECT& r, const std::wstring& label, HFONT f, bool hover);
  RECT  R(int l, int t, int r, int b);
  int   TextW(HDC dc, HFONT f, const std::wstring& s);
}
