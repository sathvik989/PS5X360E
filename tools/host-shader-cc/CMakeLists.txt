# Xenia Edge's shader compiler (tools/build/shader_cc.cc) built for the build
# machine. The PS5 build is cross-compiled, so the copy its own CMake would make
# is a PS5 program; this one runs here and is handed to it as XE_HOST_SHADER_CC.
#
#   cmake -S tools/host-shader-cc -B build/host-shader-cc -G Ninja \
#     -DCMAKE_BUILD_TYPE=Release -DXENIA_SOURCE=<Xenia Edge tree>
#   ninja -C build/host-shader-cc xenia-shader-cc
#
# The targets are the ones Xenia Edge's third_party/CMakeLists.txt defines for
# it (SPIRV-Tools, glslang with its optimizer bridge).
cmake_minimum_required(VERSION 3.20)
project(ps5x360e_host_shader_cc LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT XENIA_SOURCE)
  message(FATAL_ERROR "Set XENIA_SOURCE to the Xenia Edge tree.")
endif()
set(TP "${XENIA_SOURCE}/third_party")

set(SPIRV-Headers_SOURCE_DIR "${TP}/SPIRV-Headers" CACHE INTERNAL "")
set(SPIRV_SKIP_TESTS ON CACHE INTERNAL "")
set(SPIRV_SKIP_EXECUTABLES ON CACHE INTERNAL "")
set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE INTERNAL "")
set(SPIRV_WERROR OFF CACHE INTERNAL "")
set(SPIRV_BUILD_FUZZER OFF CACHE INTERNAL "")
add_subdirectory("${TP}/SPIRV-Tools" SPIRV-Tools EXCLUDE_FROM_ALL)

include("${TP}/glslang/parse_version.cmake")
parse_version("${TP}/glslang/CHANGES.md" GLSLANG)
set(major ${GLSLANG_VERSION_MAJOR})
set(minor ${GLSLANG_VERSION_MINOR})
set(patch ${GLSLANG_VERSION_PATCH})
set(flavor ${GLSLANG_VERSION_FLAVOR})
configure_file("${TP}/glslang/build_info.h.tmpl"
  "${CMAKE_CURRENT_BINARY_DIR}/glslang-build-info/glslang/build_info.h" @ONLY)
file(GLOB _glslang_mi
  "${TP}/glslang/glslang/MachineIndependent/*.cpp"
  "${TP}/glslang/glslang/MachineIndependent/preprocessor/*.cpp"
  "${TP}/glslang/glslang/GenericCodeGen/*.cpp")
file(GLOB _glslang_spirv_all "${TP}/glslang/SPIRV/*.cpp")
add_library(glslang STATIC
  ${_glslang_mi}
  ${_glslang_spirv_all}
  "${TP}/glslang/glslang/ResourceLimits/ResourceLimits.cpp"
  "${TP}/glslang/glslang/OSDependent/Unix/ossource.cpp")
target_include_directories(glslang PUBLIC
  "${TP}/glslang" "${CMAKE_CURRENT_BINARY_DIR}/glslang-build-info")
target_compile_definitions(glslang PRIVATE ENABLE_OPT=1)
target_link_libraries(glslang PUBLIC SPIRV-Tools-opt SPIRV-Tools-static)
find_package(Threads REQUIRED)
target_link_libraries(glslang PUBLIC Threads::Threads)

add_executable(xenia-shader-cc "${XENIA_SOURCE}/tools/build/shader_cc.cc")
target_link_libraries(xenia-shader-cc PRIVATE glslang SPIRV-Tools-static)
target_include_directories(xenia-shader-cc PRIVATE "${TP}/glslang/StandAlone")
