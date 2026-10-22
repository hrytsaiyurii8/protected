cmake_minimum_required(VERSION 3.25)
project(ScrDisplayService)

set(CMAKE_CXX_STANDARD 17)

add_library(ScrDisplaySvc SHARED
    Capture.cpp
    Composite.cpp
    Ipc.cpp
    Service.cpp
)

target_compile_definitions(ScrDisplaySvc PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(ScrDisplaySvc PRIVATE
    d3d11
    dxgi
    d3dcompiler
    windowscodecs
    avrt
)

set_target_properties(ScrDisplaySvc PROPERTIES
    OUTPUT_NAME "ScrDisplaySvc"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
)
