#pragma once
#include <string>
#include <vector>

std::wstring ExeDir();
std::wstring ConfigPath();
std::string  LoadApiKey();                       // [itch] api_key з config.ini
void         SaveApiKey(const std::string& key);

std::wstring GetInstalledPath(long long gameId); // installed.ini
void         SetInstalledPath(long long gameId, const std::wstring& path);

std::vector<std::wstring> LoadLocalGames();      // власні .exe, додані через ADD A GAME
void         SaveLocalGames(const std::vector<std::wstring>& paths);
