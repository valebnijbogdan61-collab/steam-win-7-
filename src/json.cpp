#include "json.h"
#include <stdlib.h>
#include <string.h>

const Json& Json::operator[](const std::string& k) const {
  static const Json kNull;
  std::map<std::string, Json>::const_iterator it = obj.find(k);
  return it == obj.end() ? kNull : it->second;
}

namespace {

struct P {
  const std::string& s;
  size_t i;
  explicit P(const std::string& t) : s(t), i(0) {}
};

void Ws(P& p) {
  while (p.i < p.s.size() && (p.s[p.i] == ' ' || p.s[p.i] == '\n' || p.s[p.i] == '\r' || p.s[p.i] == '\t')) ++p.i;
}

void AppendUtf8(std::string& out, unsigned cp) {
  if (cp < 0x80) out += (char)cp;
  else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
  else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

bool ParseStr(P& p, std::string& out) {
  ++p.i;  // пропускаємо "
  while (p.i < p.s.size() && p.s[p.i] != '"') {
    char c = p.s[p.i];
    if (c == '\\' && p.i + 1 < p.s.size()) {
      char e = p.s[++p.i];
      if (e == 'n') out += '\n';
      else if (e == 't') out += '\t';
      else if (e == 'r') out += '\r';
      else if (e == 'b') out += '\b';
      else if (e == 'f') out += '\f';
      else if (e == 'u') {
        if (p.i + 4 >= p.s.size()) return false;
        unsigned cp = (unsigned)strtoul(p.s.substr(p.i + 1, 4).c_str(), NULL, 16);
        p.i += 4;
        if (cp >= 0xD800 && cp < 0xDC00 && p.i + 6 < p.s.size() && p.s[p.i + 1] == '\\' && p.s[p.i + 2] == 'u') {
          unsigned lo = (unsigned)strtoul(p.s.substr(p.i + 3, 4).c_str(), NULL, 16);
          if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p.i += 6; }
        }
        AppendUtf8(out, cp);
      } else out += e;
    } else out += c;
    ++p.i;
  }
  if (p.i >= p.s.size()) return false;
  ++p.i;  // закриваюча "
  return true;
}

bool ParseValue(P& p, Json& v, int depth) {
  if (depth > 64) return false;
  Ws(p);
  if (p.i >= p.s.size()) return false;
  char c = p.s[p.i];
  if (c == '{') {
    v.type = Json::Obj;
    ++p.i; Ws(p);
    if (p.i < p.s.size() && p.s[p.i] == '}') { ++p.i; return true; }
    for (;;) {
      Ws(p);
      if (p.i >= p.s.size() || p.s[p.i] != '"') return false;
      std::string k;
      if (!ParseStr(p, k)) return false;
      Ws(p);
      if (p.i >= p.s.size() || p.s[p.i] != ':') return false;
      ++p.i;
      Json child;
      if (!ParseValue(p, child, depth + 1)) return false;
      v.obj[k] = child;
      Ws(p);
      if (p.i >= p.s.size()) return false;
      if (p.s[p.i] == ',') { ++p.i; continue; }
      if (p.s[p.i] == '}') { ++p.i; return true; }
      return false;
    }
  }
  if (c == '[') {
    v.type = Json::Arr;
    ++p.i; Ws(p);
    if (p.i < p.s.size() && p.s[p.i] == ']') { ++p.i; return true; }
    for (;;) {
      Json child;
      if (!ParseValue(p, child, depth + 1)) return false;
      v.arr.push_back(child);
      Ws(p);
      if (p.i >= p.s.size()) return false;
      if (p.s[p.i] == ',') { ++p.i; continue; }
      if (p.s[p.i] == ']') { ++p.i; return true; }
      return false;
    }
  }
  if (c == '"') { v.type = Json::Str; return ParseStr(p, v.str); }
  if (p.s.compare(p.i, 4, "true") == 0)  { v.type = Json::Bool; v.b = true;  p.i += 4; return true; }
  if (p.s.compare(p.i, 5, "false") == 0) { v.type = Json::Bool; v.b = false; p.i += 5; return true; }
  if (p.s.compare(p.i, 4, "null") == 0)  { v.type = Json::Null; p.i += 4; return true; }
  const char* start = p.s.c_str() + p.i;
  char* end = NULL;
  double d = strtod(start, &end);
  if (end == start) return false;
  v.type = Json::Num; v.num = d;
  p.i += (size_t)(end - start);
  return true;
}

} // namespace

bool JsonParse(const std::string& text, Json& out) {
  P p(text);
  return ParseValue(p, out, 0);
}
