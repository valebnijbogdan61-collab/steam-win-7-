#pragma once
#include <string>

// Локальний акаунт лаунчера "Steam Win7". Пароль ніколи нікуди не надсилається,
// на диску зберігається лише SHA-256 хеш.
bool HasAccount();
bool CreateAccount(const std::wstring& username, const std::wstring& password);
std::wstring AccountUsername();
