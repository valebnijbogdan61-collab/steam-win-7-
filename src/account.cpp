#include "account.h"
#include "config.h"
#include "sha256.h"
#include <windows.h>

namespace {

std::wstring AccountPath() {
    return ExeDir() + L"\\account.ini";
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return "";

    int n = WideCharToMultiByte(
        CP_UTF8,
        0,
        w.c_str(),
        (int)w.size(),
        NULL,
        0,
        NULL,
        NULL
    );

    std::string s(n, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        w.c_str(),
        (int)w.size(),
        &s[0],
        n,
        NULL,
        NULL
    );

    return s;
}

}

bool HasAccount() {
    wchar_t buf[256] = {0};

    GetPrivateProfileStringW(
        L"account",
        L"username",
        L"",
        buf,
        256,
        AccountPath().c_str()
    );

    return buf[0] != L'\0';
}

std::wstring AccountUsername() {
    wchar_t buf[256] = {0};

    GetPrivateProfileStringW(
        L"account",
        L"username",
        L"",
        buf,
        256,
        AccountPath().c_str()
    );

    return buf;
}

bool CreateAccount(
    const std::wstring& username,
    const std::wstring& password
) {
    if (username.empty() || password.empty())
        return false;

    std::string hash = Sha256Hex(ToUtf8(password));
    std::wstring path = AccountPath();
    std::wstring wideHash(hash.begin(), hash.end());

    WritePrivateProfileStringW(
        L"account",
        L"username",
        username.c_str(),
        path.c_str()
    );

    WritePrivateProfileStringW(
        L"account",
        L"hash",
        wideHash.c_str(),
        path.c_str()
    );

    return true;
}
