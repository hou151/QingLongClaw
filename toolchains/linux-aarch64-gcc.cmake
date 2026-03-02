set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

find_program(AARCH64_GCC NAMES aarch64-linux-gnu-gcc-11 aarch64-linux-gnu-gcc)
find_program(AARCH64_GXX NAMES aarch64-linux-gnu-g++-11 aarch64-linux-gnu-g++)

if (AARCH64_GCC)
  set(CMAKE_C_COMPILER "${AARCH64_GCC}")
else()
  set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
endif()

if (AARCH64_GXX)
  set(CMAKE_CXX_COMPILER "${AARCH64_GXX}")
else()
  set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
