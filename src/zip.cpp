#include "zip.h"
#include <windows.h>
#include <shlobj.h>
#include <shldisp.h>
#include <vector>
#include <algorithm>

bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir, std::wstring& error) {
  CreateDirectoryW(destDir.c_str(), NULL);

  HRESULT hr = CoInitialize(NULL);
  bool needUninit = SUCCEEDED(hr);
  if (hr == RPC_E_CHANGED_MODE) hr = S_OK;  // уже ініціалізовано (STA) іншим потоком, ок

  IShellDispatch* shell = NULL;
  Folder* destFolder = NULL;
  Folder* zipFolder = NULL;
  bool ok = false;

  hr = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void**)&shell);
  if (FAILED(hr) || !shell) { error = L"Shell COM unavailable"; goto done; }

  {
    VARIANT vDest; VariantInit(&vDest);
    vDest.vt = VT_BSTR; vDest.bstrVal = SysAllocString(destDir.c_str());
    hr = shell->NameSpace(vDest, &destFolder);
    VariantClear(&vDest);
    if (FAILED(hr) || !destFolder) { error = L"Cannot open destination folder"; goto done; }
  }
  {
    VARIANT vZip; VariantInit(&vZip);
    vZip.vt = VT_BSTR; vZip.bstrVal = SysAllocString(zipPath.c_str());
    hr = shell->NameSpace(vZip, &zipFolder);
    VariantClear(&vZip);
    if (FAILED(hr) || !zipFolder) { error = L"Cannot open archive (corrupted download?)"; goto done; }
  }
  {
    FolderItems* items = NULL;
    if (FAILED(zipFolder->Items(&items)) || !items) { error = L"Cannot read archive contents"; goto done; }
    VARIANT vItems; VariantInit(&vItems);
    vItems.vt = VT_DISPATCH; vItems.pdispVal = items;
    VARIANT vOpt; VariantInit(&vOpt);
    vOpt.vt = VT_I4; vOpt.lVal = FOF_NO_UI;  // без прогрес-вікна Windows
    hr = destFolder->CopyHere(vItems, vOpt);
    items->Release();
    ok = SUCCEEDED(hr);
    if (!ok) error = L"Extraction failed";
  }

done:
  if (zipFolder) zipFolder->Release();
  if (destFolder) destFolder->Release();
  if (shell) shell->Release();
  if (needUninit) CoUninitialize();
  return ok;
}

namespace {
void Scan(const std::wstring& dir, std::vector<std::pair<unsigned long long, std::wstring> >& found, int depth) {
  if (depth > 6) return;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
    std::wstring path = dir + L"\\" + fd.cFileName;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      Scan(path, found, depth + 1);
    } else {
      size_t n = wcslen(fd.cFileName);
      if (n > 4 && _wcsicmp(fd.cFileName + n - 4, L".exe") == 0) {
        std::wstring low(fd.cFileName);
        CharLowerBuffW(&low[0], (DWORD)low.size());
        if (low.find(L"unins") != std::wstring::npos || low.find(L"redist") != std::wstring::npos ||
            low.find(L"vcredist") != std::wstring::npos || low.find(L"dotnet") != std::wstring::npos ||
            low.find(L"directx") != std::wstring::npos || low.find(L"crashpad") != std::wstring::npos ||
            low.find(L"crashhandler") != std::wstring::npos)
          continue;
        unsigned long long sz = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        found.push_back(std::make_pair(sz, path));
      }
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}
}

std::wstring FindMainExe(const std::wstring& dir) {
  std::vector<std::pair<unsigned long long, std::wstring> > found;
  Scan(dir, found, 0);
  if (found.empty()) return L"";
  std::sort(found.begin(), found.end());
  return found.back().second;  // найбільший за розміром .exe зазвичай і є грою
}
