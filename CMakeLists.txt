cmake_minimum_required(VERSION 3.21)

project(
  vsrun
  VERSION 1.0.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

FetchContent_Declare(
  subprocess
  GIT_REPOSITORY https://github.com/shediao/subprocess.hpp.git
  GIT_TAG main)
FetchContent_MakeAvailable(subprocess)

FetchContent_Declare(
  argparse
  GIT_REPOSITORY https://github.com/shediao/argparse.hpp.git
  GIT_TAG main)
FetchContent_MakeAvailable(argparse)

add_compile_definitions(vsrun PRIVATE UNICODE _UNICODE)
include_directories(${PROJECT_SOURCE_DIR}/include/vssetup)
if(MSVC)
  add_compile_options(/source-charset:utf-8 /EHsc)
endif()

add_library(visualstudio_search src/visualstudio.cc)
add_executable(vsrun src/vsrun.cc)

target_link_libraries(vsrun PRIVATE subprocess::subprocess argparse::argparse
                                    visualstudio_search)

add_executable(vs-install-dir src/vs-install-dir.cc)
target_link_libraries(
  vs-install-dir PRIVATE subprocess::subprocess argparse::argparse
                         visualstudio_search)
