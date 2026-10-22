cmake_minimum_required(VERSION 3.25)
project(ScrDisplayDriver)

find_package(WDK REQUIRED)

# UMDF2 is a user-mode DLL — wdk_add_driver is KMDF-only, cannot be used here
add_library(ScrDisplay SHARED
    Driver.cpp
)

set_target_properties(ScrDisplay PROPERTIES
    OUTPUT_NAME "ScrDisplay"
    PREFIX ""
)

target_include_directories(ScrDisplay PRIVATE
    "${WDK_ROOT}/Include/${WDK_VERSION}/umdf/2.33"
    "${WDK_ROOT}/Include/${WDK_VERSION}/shared"
    "${WDK_ROOT}/Include/${WDK_VERSION}/um"
    "${WDK_ROOT}/Include/${WDK_VERSION}/km"
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(ScrDisplay PRIVATE
    POOL_NX_OPTIN=1
    UMDF_USING_NTSTATUS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _AMD64_
    NTDDI_VERSION=0x0A000007
    _WIN32_WINNT=0x0A00
)

target_link_libraries(ScrDisplay PRIVATE
    "${WDK_ROOT}/Lib/${WDK_VERSION}/um/x64/WUDFx02000.lib"
    "${WDK_ROOT}/Lib/${WDK_VERSION}/um/x64/IddCx.lib"
    OneCoreUAP
)

configure_file(ScrDisplay.inf ${CMAKE_CURRENT_BINARY_DIR}/ScrDisplay.inf COPYONLY)
