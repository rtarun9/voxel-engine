#include "voxel-engine/camera.hpp"
#include "voxel-engine/filesystem.hpp"
#include "voxel-engine/renderer.hpp"
#include "voxel-engine/shader_compiler.hpp"
#include "voxel-engine/timer.hpp"
#include "voxel-engine/voxel.hpp"
#include "voxel-engine/window.hpp"

#include "shaders/interop/render_resources.hlsli"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

int main()
{
    printf("Executable path :: %s\n", FileSystem::instance().executable_path().c_str());

    const Window window{};
    Renderer renderer(window.get_handle(), window.get_width(), window.get_height());

    // Setup imgui.
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGui::StyleColorsDark();

        D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle =
            renderer.m_cbv_srv_uav_descriptor_heap.current_cpu_descriptor_handle;

        D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor_handle =
            renderer.m_cbv_srv_uav_descriptor_heap.current_gpu_descriptor_handle;

        renderer.m_cbv_srv_uav_descriptor_heap.offset_current_descriptor_handles();

        // Setup platform / renderer backend.
        ImGui_ImplWin32_Init(window.get_handle());
        ImGui_ImplDX12_Init(renderer.m_device.Get(), Renderer::NUMBER_OF_BACKBUFFERS, Renderer::BACKBUFFER_FORMAT,
                            renderer.m_cbv_srv_uav_descriptor_heap.descriptor_heap.Get(), cpu_descriptor_handle,
                            gpu_descriptor_handle);
    }

    ChunkManager chunk_manager{};

    SceneConstantBuffer scene_buffer_data{};
    ConstantBuffer scene_buffer = renderer.create_constant_buffer(sizeof(SceneConstantBuffer));

    // Compile the vertex and pixel shader.
    Microsoft::WRL::ComPtr<IDxcBlob> vertex_shader_blob = ShaderCompiler::compile(
        FileSystem::instance().get_relative_path_wstr(L"shaders/voxel_shader.hlsl").c_str(), L"vs_main", L"vs_6_6");

    Microsoft::WRL::ComPtr<IDxcBlob> pixel_shader_blob = ShaderCompiler::compile(
        FileSystem::instance().get_relative_path_wstr(L"shaders/voxel_shader.hlsl").c_str(), L"ps_main", L"ps_6_6");

    // Setup depth buffer.
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_buffer_resource{};
    const D3D12_RESOURCE_DESC depth_buffer_resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION::D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width = window.get_width(),
        .Height = window.get_height(),
        .DepthOrArraySize = 1u,
        .MipLevels = 1u,
        .Format = DXGI_FORMAT_D32_FLOAT,
        .SampleDesc = {1u, 0u},
        .Layout = D3D12_TEXTURE_LAYOUT::D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
    };

    const D3D12_HEAP_PROPERTIES depth_buffer_heap_properties = {
        .Type = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 0u,
        .VisibleNodeMask = 0u,
    };
    const D3D12_CLEAR_VALUE depth_buffer_optimized_clear_value = {
        .Format = DXGI_FORMAT_D32_FLOAT,
        .DepthStencil = {.Depth = 1.0f, .Stencil = 1u},
    };

    throw_if_failed(renderer.m_device->CreateCommittedResource(
        &depth_buffer_heap_properties, D3D12_HEAP_FLAG_NONE, &depth_buffer_resource_desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_buffer_optimized_clear_value, IID_PPV_ARGS(&depth_buffer_resource)));

    // Create DSV.
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = renderer.m_dsv_descriptor_heap.current_cpu_descriptor_handle;
    {
        const D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
            .Format = DXGI_FORMAT_D32_FLOAT,
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Flags = D3D12_DSV_FLAG_NONE,
            .Texture2D =
                {

                    .MipSlice = 0u,
                },
        };

        renderer.m_dsv_descriptor_heap.offset_current_descriptor_handles();

        renderer.m_device->CreateDepthStencilView(depth_buffer_resource.Get(), &dsv_desc, dsv_handle);
    }

    // Create the PSO.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso{};
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_pso_desc = {
        .pRootSignature = renderer.m_bindless_root_signature.Get(),
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
                .FillMode = D3D12_FILL_MODE_WIREFRAME,
                .CullMode = D3D12_CULL_MODE_BACK,
                .FrontCounterClockwise = FALSE,
            },
        .DepthStencilState =
            {
                .DepthEnable = TRUE,
                .DepthWriteMask = D3D12_DEPTH_WRITE_MASK::D3D12_DEPTH_WRITE_MASK_ALL,
                .DepthFunc = D3D12_COMPARISON_FUNC_LESS,
                .StencilEnable = FALSE,
            },
        .InputLayout =
            {
                .NumElements = 0u,
            },
        .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .NumRenderTargets = 1u,
        .RTVFormats =
            {
                Renderer::BACKBUFFER_FORMAT,
            },
        .DSVFormat = DXGI_FORMAT_D32_FLOAT,
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
    renderer.m_direct_queue.execute_command_list();
    renderer.m_direct_queue.flush_queue();

    Camera camera{};
    Timer timer{};
    float delta_time = 0.0f;

    u64 frame_count = 0;

    bool quit{false};
    while (!quit)
    {
        if (frame_count == 0u)
        {
            // Test to check the async copy queue capabilities!
            for (int i = 1; i < ChunkManager::NUMBER_OF_CHUNKS; i++)
            {
                chunk_manager.create_chunk(renderer, i);
            }
        }

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

        // See if any chunk has been setup, and is to be added to loaded chunks.
        chunk_manager.move_to_loaded_chunks(renderer.m_copy_queue.m_fence->GetCompletedValue());

        const float window_aspect_ratio = static_cast<float>(window.get_width()) / window.get_height();

        const DirectX::XMMATRIX view_projection_matrix =
            camera.update_and_get_view_matrix(delta_time) *
            DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(45.0f), window_aspect_ratio, 0.1f, 10.0f);
        scene_buffer_data.view_projection_matrix = view_projection_matrix;

        scene_buffer.update(&scene_buffer_data);

        const auto &swapchain_index = renderer.m_swapchain_backbuffer_index;

        // Reset command allocator and command list.
        renderer.m_copy_queue.reset(swapchain_index);
        renderer.m_direct_queue.reset(swapchain_index);

        const auto &command_list = renderer.m_direct_queue.m_command_list;

        const auto &rtv_handle = renderer.m_swapchain_backbuffer_cpu_descriptor_handles[swapchain_index];
        const Microsoft::WRL::ComPtr<ID3D12Resource> swapchain_resource = renderer.m_resources[swapchain_index];

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

        // Now, clear the RTV and DSV.
        const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0u, nullptr);
        command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 1u, 0u,
                                            nullptr);

        command_list->OMSetRenderTargets(1u, &rtv_handle, FALSE, &dsv_handle);

        // Set viewport.
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor_rect);

        // Start the Dear ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ID3D12DescriptorHeap *const *shader_visible_descriptor_heaps = {
            renderer.m_cbv_srv_uav_descriptor_heap.descriptor_heap.GetAddressOf(),
        };

        command_list->SetDescriptorHeaps(1u, shader_visible_descriptor_heaps);

        // Set the index buffer, pso and all config settings for rendering.
        command_list->SetGraphicsRootSignature(renderer.m_bindless_root_signature.Get());
        command_list->SetPipelineState(pso.Get());

        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        for (size_t k = 0; k < chunk_manager.m_loaded_chunks.size(); k++)
        {
            const size_t i = chunk_manager.m_loaded_chunks[k].m_chunk_index;

            const VoxelRenderResources render_resources = {
                .position_buffer_index = static_cast<u32>(chunk_manager.m_chunk_position_buffers[i].srv_index),
                .color_buffer_index = static_cast<u32>(chunk_manager.m_chunk_color_buffers[i].srv_index),
                .chunk_index = static_cast<u32>(i),
                .scene_constant_buffer_index = static_cast<u32>(scene_buffer.cbv_index),
            };

            command_list->SetGraphicsRoot32BitConstants(0u, 64u, &render_resources, 0u);
            command_list->DrawInstanced((u32)chunk_manager.m_chunk_number_of_vertices[i], 1u, 0u, 0u);
        }

        // Render UI.
        ImGui::Begin("Debug Controller");
        ImGui::SliderFloat("movement_speed", &camera.m_movement_speed, 0.0f, 10.0f);
        ImGui::SliderFloat("rotation_speed", &camera.m_rotation_speed, 0.0f, 10.0f);
        ImGui::End();

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list.Get());

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
        renderer.m_direct_queue.execute_command_list();

        renderer.m_copy_queue.execute_command_list();

        // Now, present the rendertarget and signal command queue.
        throw_if_failed(renderer.m_swapchain->Present(1u, 0u));
        renderer.m_direct_queue.signal_fence(renderer.m_swapchain_backbuffer_index);
        renderer.m_copy_queue.signal_fence(renderer.m_swapchain_backbuffer_index);

        renderer.m_swapchain_backbuffer_index = static_cast<u8>(renderer.m_swapchain->GetCurrentBackBufferIndex());

        // Wait for the previous frame (that is presenting to swpachain_backbuffer_index) to complete execution.
        // NOTE : Do NOT do this for the copy queue if you want async copy.
        renderer.m_direct_queue.wait_for_fence_value_at_index(renderer.m_swapchain_backbuffer_index);

        ++frame_count;

        timer.stop();
        delta_time = timer.get_delta_time();
    }

    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    renderer.m_direct_queue.flush_queue();
    renderer.m_copy_queue.flush_queue();

    return 0;
}