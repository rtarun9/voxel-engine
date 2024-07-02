include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG v1.90.8
    GIT_PROGRESS TRUE
)

FetchContent_MakeAvailable(imgui)

add_library(libimgui
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)

target_include_directories(libimgui PUBLIC 
${imgui_SOURCE_DIR} 
${imgui_SOURCE_DIR}/backends
)

target_link_libraries(libimgui PUBLIC d3d12 dxguid dxgi)

add_library(external INTERFACE)
target_link_libraries(external INTERFACE libimgui)