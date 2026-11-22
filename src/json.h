#pragma once
#include <string>
#include <vector>
#include <map>

// Мінімальний JSON-парсер без бібліотек
struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type;
  bool b;
  double num;
  std::string str;
  std::vector<Json> arr;
  std::map<std::string, Json> obj;
  Json() : type(Null), b(false), num(0) {}
  const Json& operator[](const std::string& k) const;  // Null, якщо поля нема
  std::string S() const { return type == Str ? str : std::string(); }
  long long I() const { return type == Num ? (long long)num : 0; }
  bool B() const { return type == Bool && b; }
};

bool JsonParse(const std::string& text, Json& out);
