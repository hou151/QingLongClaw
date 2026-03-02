#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD_DIR="${1:-${ROOT_DIR}/build}"
JOBS="${QINGLONGCLAW_BUILD_JOBS:-$(nproc)}"

clean_mismatched_cmake_cache() {
  local root_dir="$1"
  local build_dir="$2"
  local cache_file="${build_dir}/CMakeCache.txt"
  if [[ ! -f "${cache_file}" ]]; then
    return
  fi

  local cached_source_dir=""
  cached_source_dir="$(grep '^CMAKE_HOME_DIRECTORY:INTERNAL=' "${cache_file}" | head -n1 | cut -d= -f2- || true)"
  if [[ -z "${cached_source_dir}" ]]; then
    return
  fi
  if [[ "${cached_source_dir}" == "${root_dir}" ]]; then
    return
  fi

  echo "Detected CMake source path mismatch:"
  echo "  cache: ${cached_source_dir}"
  echo "  now:   ${root_dir}"
  echo "Cleaning stale CMake cache in ${build_dir} ..."
  rm -rf "${build_dir}/CMakeCache.txt" "${build_dir}/CMakeFiles"
}

clean_mismatched_cmake_cache "${ROOT_DIR}" "${BUILD_DIR}"

CMAKE_ARGS=(
  -DCMAKE_BUILD_TYPE=Release
)

if command -v g++-11 >/dev/null 2>&1 && command -v gcc-11 >/dev/null 2>&1; then
  echo "Using GCC 11 toolchain (gcc-11/g++-11)."
  CMAKE_ARGS+=(
    -DCMAKE_C_COMPILER="$(command -v gcc-11)"
    -DCMAKE_CXX_COMPILER="$(command -v g++-11)"
  )
fi

SELECTED_CXX="${CXX:-}"
if [[ -z "${SELECTED_CXX}" ]]; then
  if command -v g++-11 >/dev/null 2>&1; then
    SELECTED_CXX="$(command -v g++-11)"
  elif command -v c++ >/dev/null 2>&1; then
    SELECTED_CXX="$(command -v c++)"
  fi
fi

if [[ -n "${SELECTED_CXX}" ]]; then
  CXX_VERSION_RAW="$("${SELECTED_CXX}" -dumpfullversion -dumpversion 2>/dev/null || true)"
  CXX_MAJOR="${CXX_VERSION_RAW%%.*}"
  if [[ "${CXX_MAJOR}" =~ ^[0-9]+$ ]] && (( CXX_MAJOR < 11 )) && [[ -z "${QINGLONGCLAW_BUILD_JOBS:-}" ]]; then
    JOBS=1
    echo "Detected GCC ${CXX_MAJOR}; forcing single-job build (set QINGLONGCLAW_BUILD_JOBS to override)."
  fi
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" -j"${JOBS}"

cp "${BUILD_DIR}/qinglongclaw" "${ROOT_DIR}/qinglongclaw-bin"
chmod +x "${ROOT_DIR}/qinglongclaw-bin"

echo "Build complete: ${BUILD_DIR}/qinglongclaw"
echo "Runnable binary: ${ROOT_DIR}/qinglongclaw-bin"
