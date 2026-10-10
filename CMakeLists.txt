cmake_minimum_required(VERSION 3.20)

project(TorrentForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TORRENTFORGE_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(TORRENTFORGE_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizers require GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(torrentforge_core STATIC src/application.cpp src/bencode.cpp)
target_include_directories(torrentforge_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(torrentforge_core SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_compile_features(torrentforge_core PUBLIC cxx_std_23)

add_executable(torrentforge src/main.cpp)
target_link_libraries(torrentforge PRIVATE torrentforge_core)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
