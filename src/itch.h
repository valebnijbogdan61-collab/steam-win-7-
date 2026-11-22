#pragma once
#include <string>
#include <vector>
#include "game.h"

struct ItchProfile {
  bool ok, badKey;
  std::wstring name, url, error;
  ItchProfile() : ok(false), badKey(false) {}
};

ItchProfile ItchGetProfile(const std::string& apiKey);

// Бібліотека: куплені/заклеймлені ігри + власні (profile/owned-keys, profile/games)
bool ItchLoadLibrary(const std::string& key, std::vector<Game>& out, std::wstring& err);

// Пошук по всьому itch.io (legacy search/games)
bool ItchSearch(const std::string& key, const std::wstring& query, std::vector<Game>& out, std::wstring& err);

// Посилання на завантаження першого (Windows-)білда гри
bool ItchResolveDownload(const std::string& key, long long gameId, long long keyId,
                         std::wstring& url, std::wstring& filename, std::wstring& err);
