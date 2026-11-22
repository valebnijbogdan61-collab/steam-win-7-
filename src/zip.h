#pragma once
#include <string>

// Розпакування .zip через вбудований Shell Windows (works on Windows 7, без сторонніх бібліотек).
bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir, std::wstring& error);

// Рекурсивний пошук найбільшого .exe у папці (евристика: головний виконуваний файл гри).
std::wstring FindMainExe(const std::wstring& dir);
