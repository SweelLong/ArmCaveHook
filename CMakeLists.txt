cmake_minimum_required(VERSION 3.24)
project(armcave LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Plugins are compiled at patch time, so embed the discovered driver paths.
find_program(ARMCAVE_CLANG_EXECUTABLE NAMES clang clang.exe REQUIRED)
find_program(ARMCAVE_CLANGXX_EXECUTABLE NAMES clang++ clang++.exe REQUIRED)

# Keep implementation files and public tool headers in separate directories.
file(GLOB ARMCAVE_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

add_executable(armcave ${ARMCAVE_SOURCES})
target_include_directories(armcave PRIVATE include)
target_compile_definitions(armcave PRIVATE
    ARMCAVE_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    ARMCAVE_CLANG_PATH="${ARMCAVE_CLANG_EXECUTABLE}"
    ARMCAVE_CLANGXX_PATH="${ARMCAVE_CLANGXX_EXECUTABLE}"
)

# Install target
install(TARGETS armcave RUNTIME DESTINATION bin)
