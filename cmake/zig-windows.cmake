# Cross-compile for Windows x64 with Zig (zig cc = clang + mingw-w64 headers/libs).
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/zig-windows.cmake \
#         -DSDL2_DIR=<SDL2-devel-2.x-mingw>/x86_64-w64-mingw32/lib/cmake/SDL2
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-cc-windows)
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig-ranlib CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "-s")   # stripped release binaries
