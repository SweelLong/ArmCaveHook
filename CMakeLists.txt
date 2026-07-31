cmake_minimum_required(VERSION 3.24)
project(armcave LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

file(GLOB ARMCAVE_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/aarch64/*.cpp
)

add_executable(armcave ${ARMCAVE_SOURCES})
target_include_directories(armcave PRIVATE include)
target_compile_definitions(armcave PRIVATE
    ARMCAVE_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

install(TARGETS armcave RUNTIME DESTINATION bin)
