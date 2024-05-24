#pragma once

// The naming convention of namespaces is being broken here, mostly because this
// "namespace" is a smart way of simulating static class behaviour.
// DXC is used for shader compilation.
namespace ShaderCompiler
{
IDxcBlob *compile(const wchar_t *const file_path, const wchar_t *const entry_point, const wchar_t *const target);
}
