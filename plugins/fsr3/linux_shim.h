// Linux build shims for the FidelityFX SDK sources (written for MSVC).
#pragma once
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <locale>
#include <codecvt>
#include <string>
#include <algorithm>
#include <cmath>
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef FFX_UNUSED
#define FFX_UNUSED(x) (void)(x)
#endif
// MSVC "secure" string functions the SDK uses.
template <size_t N> inline int wcscpy_s(wchar_t (&d)[N], const wchar_t* s) { wcsncpy(d, s, N - 1); d[N - 1] = 0; return 0; }
inline int wcscpy_s(wchar_t* d, size_t n, const wchar_t* s) { wcsncpy(d, s, n - 1); d[n - 1] = 0; return 0; }
inline int strcpy_s(char* d, size_t n, const char* s) { strncpy(d, s, n - 1); d[n - 1] = 0; return 0; }
template <size_t N> inline int strcpy_s(char (&d)[N], const char* s) { strncpy(d, s, N - 1); d[N - 1] = 0; return 0; }
#define sprintf_s(buf, size, ...) snprintf(buf, size, __VA_ARGS__)
