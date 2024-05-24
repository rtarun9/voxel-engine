#include "pch.hpp"

#include "common.hpp"
#include "shader_compiler.hpp"

namespace ShaderCompiler
{
// Core DXC objects.
Microsoft::WRL::ComPtr<IDxcUtils> g_utils{};
Microsoft::WRL::ComPtr<IDxcCompiler3> g_compiler{};
Microsoft::WRL::ComPtr<IDxcIncludeHandler> g_include_handler{};

IDxcBlob *compile(const wchar_t *const file_path, const wchar_t *const entry_point, const wchar_t *const target)
{
    // Check if the compiler object has been created.
    if (!g_utils)
    {
        throw_if_failed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&g_utils)));
        throw_if_failed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&g_compiler)));
        g_utils->CreateDefaultIncludeHandler(&g_include_handler);
    }

    std::vector<LPCWSTR> compiler_arguments = {
        file_path, L"-E", entry_point, L"-T", target, DXC_ARG_PACK_MATRIX_ROW_MAJOR, DXC_ARG_WARNINGS_ARE_ERRORS};
    if constexpr (VX_DEBUG_MODE)
    {
        compiler_arguments.push_back(DXC_ARG_DEBUG);
    }
    else
    {
        compiler_arguments.emplace_back(DXC_ARG_OPTIMIZATION_LEVEL3);
    }

    // Open the source file.
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> source{};
    throw_if_failed(g_utils->LoadFile(file_path, nullptr, &source));
    const DxcBuffer source_buffer = {
        .Ptr = source->GetBufferPointer(),
        .Size = source->GetBufferSize(),
        .Encoding = DXC_CP_ACP,
    };

    // Compile the shader.
    Microsoft::WRL::ComPtr<IDxcResult> results{};
    throw_if_failed(g_compiler->Compile(&source_buffer, compiler_arguments.data(), compiler_arguments.size(),
                                        g_include_handler.Get(), IID_PPV_ARGS(&results)));

    // Check for errors.
    Microsoft::WRL::ComPtr<IDxcBlobUtf8> error_blob{};
    throw_if_failed(results->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&error_blob), nullptr));

    if (error_blob && error_blob->GetStringLength() > 0)
    {
        wprintf(L"Shader : %s has warnings and errors:\n%S\n", file_path, error_blob->GetStringPointer());
    }

    // Get the shader object and return.
    IDxcBlob *shader_blob{nullptr};
    throw_if_failed(results->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader_blob), nullptr));
    return shader_blob;
}
} // namespace ShaderCompiler