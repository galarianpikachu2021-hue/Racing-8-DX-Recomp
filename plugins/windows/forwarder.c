/* SPDX-License-Identifier: GPL-3.0-or-later
 * Frame-generation forwarder. A plugin library that offers both upscaling and frame generation (FSR 3, DLSS) is found by the host under
 * two names: kr_upscaler_<id>.dll and kr_framegen_<id>.dll. The two must be ONE loaded module (they share the upscaler's images and the
 * vendor runtime's session), and Windows has no symlink-style trick like Linux's, so kr_framegen_<id>.dll is this tiny DLL: it loads
 * the sibling kr_upscaler_<id>.dll (the same module the host loaded for upscaling) and hands back its kr_framegen_get_api.
 * Build: cl /nologo /O2 /LD forwarder.c /Fe:kr_framegen_<id>.dll   (the file name carries the id). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <wchar.h>

__declspec(dllexport) const void* kr_framegen_get_api(void) {
    HMODULE self = NULL;
    wchar_t path[MAX_PATH * 4];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&kr_framegen_get_api, &self)) {
        return NULL;
    }
    DWORD n = GetModuleFileNameW(self, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    if (n == 0 || n >= sizeof(path) / sizeof(path[0])) {
        return NULL;
    }
    wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    if (wcsncmp(name, L"kr_framegen_", 12) != 0) {
        return NULL;
    }
    wchar_t sibling[MAX_PATH * 4];
    size_t dir_len = (size_t)(name - path);
    if (dir_len + 12 + wcslen(name + 12) + 8 >= sizeof(sibling) / sizeof(sibling[0])) {
        return NULL;
    }
    wmemcpy(sibling, path, dir_len);
    wcscpy(sibling + dir_len, L"kr_upscaler_");
    wcscat(sibling, name + 12);
    HMODULE lib = LoadLibraryExW(sibling, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!lib) {
        return NULL;
    }
    const void* (*get)(void) = (const void* (*)(void))GetProcAddress(lib, "kr_framegen_get_api");
    return get ? get() : NULL;
}
