#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Applies the small edits that make AMD's FidelityFX-SC shader compiler (tools/ffx_shader_compiler in the FidelityFX SDK, MIT license)
build and run on Linux with the GLSL path only: Windows headers and path calls are replaced, wmain becomes main, and the HLSL compiler is
stubbed. The SDK is fetched by build.sh; this script only rewrites that local copy.

    port-ffx-sc.py <ffx_shader_compiler dir> <work dir>
"""
import pathlib
import shutil
import sys

src = pathlib.Path(sys.argv[1])
work = pathlib.Path(sys.argv[2])
shutil.rmtree(work, ignore_errors=True)
shutil.copytree(src / "src", work / "src")
(work / "libs").mkdir()
for lib in ("SPIRV-Reflect", "MD5", "tiny-process-library"):
    shutil.copytree(src / "libs" / lib, work / "libs" / lib)
os_src = work / "src"


def edit(name, fn):
    p = os_src / name
    p.write_text(fn(p.read_text()))


def pch(s):
    s = s.replace("#define NOMINMAX\n#define WIN32_LEAN_AND_MEAN\n#include <Windows.h>\n#include <atlcomcli.h>\n#include <dxcapi.h>\n#include <d3dcompiler.h>\n", "")
    s = s.replace("#include <d3d12shader.h>\n", "")
    s = s.replace("#include <assert.h>", "#include <assert.h>\n#include <cstring>\n#include <cwchar>\n#include <cstdint>\n#include <memory>\n#include <map>\n#include <cstdlib>\n#include <cmath>")
    return s + '''
// Linux port shims
#include <locale>
#include <codecvt>
inline int _wfopen_s(FILE** fp, const wchar_t* path, const wchar_t* mode)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    *fp = fopen(conv.to_bytes(path).c_str(), conv.to_bytes(mode).c_str());
    return *fp ? 0 : 1;
}
'''


def utils(s):
    i = s.index("std::string WCharToUTF8")
    return s[:i] + '''std::string WCharToUTF8(const std::wstring& wstr)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.to_bytes(wstr);
}

std::wstring UTF8ToWChar(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.from_bytes(str);
}
'''


def ffx_sc(s):
    s = s.replace("#include <Windows.h>\n#include <pathcch.h>\n", "")
    i = s.index("void LaunchParameters::EnsureOutputPathExistsAndMakeCanonical")
    j = s.index("void LaunchParameters::ParsePermutationOption")
    s = s[:i] + '''void LaunchParameters::EnsureOutputPathExistsAndMakeCanonical(std::wstring& inoutOutputPath)
{
    fs::path p(inoutOutputPath);
    std::error_code ec;
    fs::create_directories(p, ec);
    std::wstring out = fs::absolute(p).lexically_normal().wstring();
    if (!out.empty() && out.back() != L'/') out += L'/';
    inoutOutputPath = out;
}

''' + s[j:]
    i = s.index("std::wstring Application::MakeFullPath")
    j = s.index("void Application::GenerateMacroPermutations")
    s = s[:i] + '''std::wstring Application::MakeFullPath(const std::wstring & outputPath, const std::wstring & fileName)
{
    return (fs::path(outputPath) / fs::path(fileName)).wstring();
}

''' + s[j:]
    return s.replace("int wmain(int argc, wchar_t** argv)\n{", "int main(int argc, char** cargv)\n{\n    std::setlocale(LC_ALL, \"C.UTF-8\");\n    std::vector<std::wstring> wargs;\n    for (int i = 0; i < argc; ++i) wargs.push_back(UTF8ToWChar(cargv[i]));\n    std::vector<wchar_t*> wptrs;\n    for (auto& w : wargs) wptrs.push_back(w.data());\n    wchar_t** argv = wptrs.data();")


edit("pch.hpp", pch)
edit("utils.cpp", utils)
edit("utils.h", lambda s: s.replace('#include "DXBCChecksum.h"\n', ""))
edit("ffx_sc.cpp", ffx_sc)
(os_src / "hlsl_compiler.h").write_text('''// Linux port: HLSL (DXC/FXC) isn't available, only the GLSL path is built.
#pragma once
#include "compiler.h"
#include <stdexcept>
class HLSLCompiler : public ICompiler
{
public:
    enum Type { DXC, FXC, GDK_SCARLETT_X64, GDK_XBOXONE_X64 };
    HLSLCompiler(Type, const std::string&, const std::string& a, const std::string& b, const std::string& c, const std::string& d, bool e, bool f)
        : ICompiler(a, b, c, d, e, f)
    {
        throw std::runtime_error("HLSL compilation isn't supported in this port");
    }
    bool Compile(Permutation&, const std::vector<std::string>&, std::mutex&) override { return false; }
    bool ExtractReflectionData(Permutation&) override { return false; }
    void WriteBinaryHeaderReflectionData(FILE*, const Permutation&, std::mutex&) override {}
    void WritePermutationHeaderReflectionStructMembers(FILE*) override {}
    void WritePermutationHeaderReflectionData(FILE*, const Permutation&) override {}
};
''')
