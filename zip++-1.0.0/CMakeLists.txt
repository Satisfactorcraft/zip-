cmake_minimum_required(VERSION 3.16)
project(zip_tool VERSION 1.0 LANGUAGES CXX)

# C++17 Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler-Warnungen
if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Abhängigkeiten
find_package(ZLIB REQUIRED)

# LZMA/XZ Bibliothek manuell finden (Arch Linux)
find_path(LZMA_INCLUDE_DIR lzma.h)
find_library(LZMA_LIBRARY NAMES lzma)

if(NOT LZMA_INCLUDE_DIR OR NOT LZMA_LIBRARY)
    message(FATAL_ERROR "Could not find LZMA library or headers. Install xz package.")
endif()

message(STATUS "Found LZMA include: ${LZMA_INCLUDE_DIR}")
message(STATUS "Found LZMA library: ${LZMA_LIBRARY}")

# Executable
add_executable(zip++ zip++.cpp)

# Include Verzeichnisse
target_include_directories(zip++ PRIVATE ${LZMA_INCLUDE_DIR})

# Libraries linken
target_link_libraries(zip++ PRIVATE ZLIB::ZLIB ${LZMA_LIBRARY})
