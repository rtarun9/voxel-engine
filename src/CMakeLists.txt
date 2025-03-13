set (SRC_FILES
    "main.cpp"
    "window.cpp"
    "timer.cpp"
    "filesystem.cpp"
    "camera.cpp"
    "rhi/renderer.cpp"
    "rhi/descriptor_heap.cpp"
    "rhi/command_queue.cpp"
    "shader_compiler.cpp"
    "voxel.cpp"
)

set (HEADER_FILES
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/common.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/thread_pool.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/timer.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/types.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/window.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/rhi/renderer.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/rhi/descriptor_heap.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/rhi/buffers.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/rhi/command_queue.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/filesystem.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/camera.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/shader_compiler.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/voxel.hpp
)

add_executable(voxel-engine ${SRC_FILES} ${HEADER_FILES})
target_link_libraries(voxel-engine PUBLIC d3d12 dxgi dxguid dxcompiler external)

set_property(TARGET voxel-engine PROPERTY COMPILE_WARNING_AS_ERROR ON)

target_precompile_headers(voxel-engine PUBLIC ${CMAKE_SOURCE_DIR}/include/voxel-engine/pch.hpp)

target_include_directories(voxel-engine PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(voxel-engine PRIVATE ${CMAKE_SOURCE_DIR}/)
