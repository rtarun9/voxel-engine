#include "voxel-engine/filesystem.hpp"
#include "voxel-engine/renderer.hpp"
#include "voxel-engine/shader_compiler.hpp"
#include "voxel-engine/timer.hpp"
#include "voxel-engine/window.hpp"

int main()
{
    printf("Executable path :: %s\n", FileSystem::instance().executable_path().c_str());

    const Window window{};
    Renderer renderer(window.get_handle(), window.get_width(), window.get_height());

    const Microsoft::WRL::ComPtr<ID3D12RootSignature> bindless_root_signature = renderer.m_bindless_root_signature;

    // Setup resources required for rendering.

    // Setup index, position, and color buffer.
    IndexBuffer index_buffer{};
    {
        static constexpr std::array<u16, 3> data{0u, 1u, 2u};
        index_buffer = renderer.create_index_buffer(data.data(), sizeof(u16), data.size());
    }

    StructuredBuffer position_buffer{};
    {
        static constexpr std::array<DirectX::XMFLOAT3, 3> data{
            DirectX::XMFLOAT3{-0.5f, -0.5f, 0.0f},
            DirectX::XMFLOAT3{0.0f, 0.5f, 0.0f},
            DirectX::XMFLOAT3{0.5f, -0.5f, 0.0f},
        };

        position_buffer = renderer.create_structured_buffer(data.data(), sizeof(DirectX::XMFLOAT3), data.size());
    }

    StructuredBuffer color_buffer{};
    {
        static constexpr std::array<DirectX::XMFLOAT3, 3> data{
            DirectX::XMFLOAT3{0.0f, 1.0f, 1.0f},
            DirectX::XMFLOAT3{0.5f, 0.25f, 0.0f},
            DirectX::XMFLOAT3{1.0f, 0.0f, 1.0f},
        };

        color_buffer = renderer.create_structured_buffer(data.data(), sizeof(DirectX::XMFLOAT3), data.size());
    }

    // Compile the vertex and pixel shader.
    Microsoft::WRL::ComPtr<IDxcBlob> vertex_shader_blob = ShaderCompiler::compile(
        FileSystem::instance().get_relative_path_wstr(L"shaders/triangle_shader.hlsl").c_str(), L"vs_main", L"vs_6_6");

    Microsoft::WRL::ComPtr<IDxcBlob> pixel_shader_blob = ShaderCompiler::compile(
        FileSystem::instance().get_relative_path_wstr(L"shaders/triangle_shader.hlsl").c_str(), L"ps_main", L"ps_6_6");

    // Create the PSO.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso{};
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_pso_desc = {
        .pRootSignature = bindless_root_signature.Get(),
        .VS =
            {
                .pShaderBytecode = vertex_shader_blob->GetBufferPointer(),
                .BytecodeLength = vertex_shader_blob->GetBufferSize(),
            },
        .PS =
            {
                .pShaderBytecode = pixel_shader_blob->GetBufferPointer(),
                .BytecodeLength = pixel_shader_blob->GetBufferSize(),
            },
        .BlendState =
            {
                .AlphaToCoverageEnable = FALSE,
                .IndependentBlendEnable = FALSE,
                .RenderTarget =
                    {
                        D3D12_RENDER_TARGET_BLEND_DESC{
                            .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
                        },
                    },
            },
        .SampleMask = 0xffff'ffff,
        .RasterizerState =
            {
                .FillMode = D3D12_FILL_MODE_SOLID,
                .CullMode = D3D12_CULL_MODE_BACK,
                .FrontCounterClockwise = FALSE,
            },
        .DepthStencilState =
            {
                .DepthEnable = FALSE,
            },
        .InputLayout =
            {
                .NumElements = 0u,
            },
        .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .NumRenderTargets = 1u,
        .RTVFormats =
            {
                DXGI_FORMAT_R10G10B10A2_UNORM,
            },
        .SampleDesc =
            {
                1u,
                0u,
            },
        .NodeMask = 0u,
    };
    throw_if_failed(renderer.m_device->CreateGraphicsPipelineState(&graphics_pso_desc, IID_PPV_ARGS(&pso)));

    // Create viewport and scissor.
    const D3D12_VIEWPORT viewport = {
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = (float)window.get_width(),
        .Height = (float)window.get_height(),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };

    // The default config is used if we want to mask the entire viewport for drawing.
    const D3D12_RECT scissor_rect = {
        .left = 0u,
        .top = 0u,
        .right = LONG_MAX,
        .bottom = LONG_MAX,
    };

    // Execute and flush gpu so resources required for rendering (before the first frame) are ready.
    renderer.execute_command_list();
    renderer.flush_gpu();

    Timer timer{};

    u64 frame_count = 0;

    bool quit{false};
    while (!quit)
    {
        timer.start();

        MSG message = {};
        if (PeekMessageA(&message, NULL, 0u, 0u, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }

        if (message.message == WM_QUIT)
        {
            quit = true;
        }

        const auto &allocator = renderer.m_direct_command_allocators[renderer.m_swapchain_backbuffer_index];
        const auto &command_list = renderer.m_command_list;

        const auto &swapchain_index = renderer.m_swapchain_backbuffer_index;

        // Reset command allocator and command list.
        throw_if_failed(allocator->Reset());
        throw_if_failed(command_list->Reset(allocator.Get(), nullptr));

        const auto &rtv_handle = renderer.m_swapchain_backbuffer_cpu_descriptor_handles[swapchain_index];
        const Microsoft::WRL::ComPtr<ID3D12Resource> &swapchain_resource = renderer.m_resources[swapchain_index];

        // Transition the backbuffer from presentation mode to render target mode.
        const D3D12_RESOURCE_BARRIER presentation_to_render_target_barrier = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition =
                {
                    .pResource = swapchain_resource.Get(),
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                    .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
                },
        };

        command_list->ResourceBarrier(1u, &presentation_to_render_target_barrier);

        // Now, clear the RTV.
        const float clear_color[4] = {cosf(frame_count / 100.0f), sinf(frame_count / 100.0f), 0.0f, 1.0f};
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0u, nullptr);

        command_list->OMSetRenderTargets(1u, &rtv_handle, FALSE, nullptr);

        // Set viewport.
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor_rect);

        ID3D12DescriptorHeap *const *shader_visible_descriptor_heaps = {
            renderer.m_cbv_srv_uav_descriptor_heap.descriptor_heap.GetAddressOf(),
        };

        command_list->SetDescriptorHeaps(1u, shader_visible_descriptor_heaps);

        // Set the index buffer, pso and all config settings for rendering.
        command_list->SetGraphicsRootSignature(bindless_root_signature.Get());
        command_list->SetPipelineState(pso.Get());

        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        command_list->IASetIndexBuffer(&index_buffer.index_buffer_view);

        struct RenderResources
        {
            u32 position_buffer_index;
            u32 color_buffer_index;
        };

        RenderResources render_resources = {
            .position_buffer_index = static_cast<u32>(position_buffer.srv_index),
            .color_buffer_index = static_cast<u32>(color_buffer.srv_index),
        };

        command_list->SetGraphicsRoot32BitConstants(0u, 64u, &render_resources, 0u);
        command_list->DrawIndexedInstanced(3u, 1u, 0u, 0u, 0u);

        // Now, transition back to presentation mode.
        const D3D12_RESOURCE_BARRIER render_target_to_presentation_barrier = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition =
                {
                    .pResource = swapchain_resource.Get(),
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                    .StateAfter = D3D12_RESOURCE_STATE_PRESENT,
                },
        };

        command_list->ResourceBarrier(1u, &render_target_to_presentation_barrier);

        // Submit command list to queue for execution.
        renderer.execute_command_list();

        // Now, present the rendertarget and signal command queue.
        throw_if_failed(renderer.m_swapchain->Present(1u, 0u));
        renderer.signal_fence();

        renderer.m_swapchain_backbuffer_index = static_cast<u8>(renderer.m_swapchain->GetCurrentBackBufferIndex());

        // Wait for the previous frame (that is presenting to swpachain_backbuffer_index) to complete execution.
        renderer.wait_for_fence_value_at_index(renderer.m_swapchain_backbuffer_index);

        ++frame_count;

        timer.stop();
    }

    return 0;
}