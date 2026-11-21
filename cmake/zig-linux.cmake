# Cross-compile for Linux x64 (glibc >= 2.28) with Zig.
#   cmake -B build-linux -DCMAKE_TOOLCHAIN_FILE=cmake/zig-linux.cmake -DSDL2_DIR=<dir with sdl2-config.cmake>
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-cc-linux)
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig-ranlib CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "-s")   # stripped release binaries
