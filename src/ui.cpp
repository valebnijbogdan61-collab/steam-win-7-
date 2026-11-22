#include "ui.h"

namespace ui {

HFONT MakeFont(int px, bool bold) {
  return CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

RECT R(int l, int t, int r, int b) { RECT x = { l, t, r, b }; return x; }

void Fill(HDC dc, const RECT& r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

void Frame(HDC dc, const RECT& r, COLORREF c, int t) {
  Fill(dc, R(r.left, r.top, r.right, r.top + t), c);
  Fill(dc, R(r.left, r.bottom - t, r.right, r.bottom), c);
  Fill(dc, R(r.left, r.top, r.left + t, r.bottom), c);
  Fill(dc, R(r.right - t, r.top, r.right, r.bottom), c);
}

void VGradient(HDC dc, const RECT& r, COLORREF t, COLORREF b) {
  if (r.right <= r.left || r.bottom <= r.top) return;
  TRIVERTEX v[2];
  v[0].x = r.left;  v[0].y = r.top;
  v[0].Red = (COLOR16)(GetRValue(t) << 8); v[0].Green = (COLOR16)(GetGValue(t) << 8);
  v[0].Blue = (COLOR16)(GetBValue(t) << 8); v[0].Alpha = 0;
  v[1].x = r.right; v[1].y = r.bottom;
  v[1].Red = (COLOR16)(GetRValue(b) << 8); v[1].Green = (COLOR16)(GetGValue(b) << 8);
  v[1].Blue = (COLOR16)(GetBValue(b) << 8); v[1].Alpha = 0;
  GRADIENT_RECT g = { 0, 1 };
  GradientFill(dc, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
}

void Text(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fmt) {
  HFONT o = (HFONT)SelectObject(dc, f);
  SetTextColor(dc, c);
  SetBkMode(dc, TRANSPARENT);
  DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt | DT_NOPREFIX);
  SelectObject(dc, o);
}

void ProgressBar(HDC dc, const RECT& r, double f) {
  Fill(dc, r, theme::kDarkest);
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  RECT p = r;
  p.right = r.left + (LONG)((r.right - r.left) * f);
  VGradient(dc, p, theme::kAccent, RGB(0x3a, 0x8f, 0xc4));
}

void GreenButton(HDC dc, const RECT& r, const std::wstring& label, HFONT f, bool hover) {
  VGradient(dc, r, hover ? RGB(0x8f, 0xd0, 0x2c) : RGB(0x75, 0xb0, 0x22),
                   hover ? RGB(0x6a, 0xa3, 0x21) : RGB(0x58, 0x8a, 0x1b));
  Text(dc, label, r, f, theme::kWhite, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

int TextW(HDC dc, HFONT f, const std::wstring& s) {
  HFONT o = (HFONT)SelectObject(dc, f);
  SIZE sz = { 0, 0 };
  GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
  SelectObject(dc, o);
  return sz.cx;
}

} // namespace ui
