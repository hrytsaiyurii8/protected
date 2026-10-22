cmake_minimum_required(VERSION 3.25)
project(ScrDisplayDriver)

# Requires WDK to be installed and WDK CMake helpers on the module path.
# On the GitHub Actions runner this is set up by the workflow before invoking cmake.
find_package(WDK REQUIRED)

wdk_add_driver(ScrDisplay
    UMDF 2.33
    Driver.cpp
)

target_include_directories(ScrDisplay PRIVATE
    ${WDK_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(ScrDisplay PRIVATE
    POOL_NX_OPTIN=1
)

# Copy INF alongside the built DLL so pnputil can find both
configure_file(ScrDisplay.inf ${CMAKE_CURRENT_BINARY_DIR}/ScrDisplay.inf COPYONLY)
