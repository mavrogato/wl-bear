
cmake_minimum_required(VERSION 3.22)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_COMPILER "clang") # intel/llvm private build
set(CMAKE_C_STANDARD "17")
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_CXX_STANDARD 23)

project(wl-bear)

#############################################################################
include(FetchContent)
FetchContent_Populate(aux
  GIT_REPOSITORY https://github.com/mavrogato/aux
  GIT_TAG main)

#############################################################################
include_directories(
  ${aux_SOURCE_DIR}/include)

add_executable(wl-bear
  wl-bear.cc)

target_compile_options(wl-bear
  PRIVATE
  -fsycl
  -fsycl-targets=nvptx64-nvidia-cuda
  -Wno-unknown-cuda-version
  -Wall
  -Wextra)

add_custom_target(run
  DEPENDS wl-bear
  COMMAND WAYLAND_DEBUG=1 ./wl-bear)
