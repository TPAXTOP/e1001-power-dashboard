#include "semver.h"

#include <ctype.h>
#include <string.h>

namespace dash {

struct Version {
  unsigned long core[3];
  const char* pre;  // prerelease identifiers, not null-terminated; nullptr = none
  size_t preLen;
};

static bool identChar(char c) { return isalnum((unsigned char)c) || c == '-'; }

// Dot-separated identifiers [0-9A-Za-z-]+, no empty ones.
static bool validIdents(const char* s, size_t len) {
  if (len == 0 || s[0] == '.' || s[len - 1] == '.') return false;
  for (size_t i = 0; i < len; i++) {
    if (s[i] == '.') {
      if (s[i + 1] == '.') return false;
    } else if (!identChar(s[i])) {
      return false;
    }
  }
  return true;
}

static bool parse(const char* s, Version& v) {
  if (!s) return false;
  for (int i = 0; i < 3; i++) {
    if (!isdigit((unsigned char)*s)) return false;
    unsigned long n = 0;
    int digits = 0;
    while (isdigit((unsigned char)*s)) {
      if (++digits > 9) return false;  // no overflow, far beyond any real version
      n = n * 10 + (*s++ - '0');
    }
    v.core[i] = n;
    if (i < 2 && *s++ != '.') return false;
  }
  v.pre = nullptr;
  v.preLen = 0;
  if (*s == '-') {
    v.pre = ++s;
    while (*s && *s != '+') s++;
    v.preLen = s - v.pre;
    if (!validIdents(v.pre, v.preLen)) return false;
  }
  if (*s == '+') {
    s++;
    if (!validIdents(s, strlen(s))) return false;
    s += strlen(s);
  }
  return *s == '\0';
}

static bool allDigits(const char* s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (!isdigit((unsigned char)s[i])) return false;
  }
  return true;
}

// Numeric identifiers compare numerically and sort before alphanumeric ones.
static int compareIdent(const char* a, size_t al, const char* b, size_t bl) {
  bool an = allDigits(a, al), bn = allDigits(b, bl);
  if (an && bn) {
    while (al > 1 && *a == '0') a++, al--;
    while (bl > 1 && *b == '0') b++, bl--;
    if (al != bl) return al < bl ? -1 : 1;
    return memcmp(a, b, al);
  }
  if (an != bn) return an ? -1 : 1;
  int c = memcmp(a, b, al < bl ? al : bl);
  if (c) return c;
  return al == bl ? 0 : (al < bl ? -1 : 1);
}

static int comparePre(const char* a, size_t al, const char* b, size_t bl) {
  size_t i = 0, j = 0;
  while (i < al && j < bl) {
    size_t ie = i, je = j;
    while (ie < al && a[ie] != '.') ie++;
    while (je < bl && b[je] != '.') je++;
    int c = compareIdent(a + i, ie - i, b + j, je - j);
    if (c) return c;
    i = ie + 1;
    j = je + 1;
  }
  // All shared identifiers equal: the longer list wins.
  bool aMore = i < al, bMore = j < bl;
  return aMore == bMore ? 0 : (aMore ? 1 : -1);
}

bool semverValid(const char* v) {
  Version p;
  return parse(v, p);
}

int semverCompare(const char* a, const char* b) {
  Version va, vb;
  parse(a, va);
  parse(b, vb);
  for (int i = 0; i < 3; i++) {
    if (va.core[i] != vb.core[i]) return va.core[i] < vb.core[i] ? -1 : 1;
  }
  if (!va.pre || !vb.pre) {
    if (va.pre == vb.pre) return 0;
    return va.pre ? -1 : 1;  // the prerelease sorts first
  }
  int c = comparePre(va.pre, va.preLen, vb.pre, vb.preLen);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

}  // namespace dash
