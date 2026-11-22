#pragma once
#include <windows.h>
#include <string>

void ImgInit();
void ImgShutdown();
HBITMAP DecodeImage(const std::string& data);   // jpg/png/gif -> HBITMAP (GDI+, є в Windows 7)
