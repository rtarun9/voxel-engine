set (SRC_FILES
    "main.cpp"
    "window.cpp"
    "timer.cpp"
    "filesystem.cpp"
    "renderer.cpp"
)

set (HEADER_FILES
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/common.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/timer.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/types.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/window.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/renderer.hpp
    ${CMAKE_SOURCE_DIR}/include/voxel-engine/filesystem.hpp
)

add_executable(voxel-engine ${SRC_FILES} ${HEADER_FILES})
target_link_libraries(voxel-engine PUBLIC d3d12 dxgi dxguid)

# Treat warnings as errors.
target_compile_options(voxel-engine PRIVATE  
    $<$<CXX_COMPILER_ID:MSVC>: /W4>)

# Setup PCH.
target_precompile_headers(voxel-engine PUBLIC ${CMAKE_SOURCE_DIR}/include/voxel-engine/pch.hpp)

target_include_directories(voxel-engine PRIVATE ${CMAKE_SOURCE_DIR}/include)