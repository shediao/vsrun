cmake_minimum_required(VERSION 3.21)

project(
  vsrun
  VERSION 1.0.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_executable(vsrun src/vsrun.cc)

if(MSVC)
  target_compile_options(vsrun PRIVATE /source-charset:utf-8 /EHsc)
endif()
target_include_directories(vsrun PRIVATE ${PROJECT_SOURCE_DIR}/include/vssetup)
target_link_libraries(vsrun PRIVATE subprocess::subprocess argparse::argparse)
