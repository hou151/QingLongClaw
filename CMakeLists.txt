cmake_minimum_required(VERSION 3.18)

project(QingLongClaw VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(
  qinglongclaw
  src/main.cpp
  src/cli.cpp
  src/util.cpp
  src/config.cpp
  src/model_preset.cpp
  src/http_client.cpp
  src/provider.cpp
  src/tools.cpp
  src/session_store.cpp
  src/auth.cpp
  src/cron_service.cpp
  src/skills.cpp
  src/agent.cpp
  src/gateway.cpp
  src/version.cpp
)

target_include_directories(
  qinglongclaw
  PRIVATE
    include
    third_party
)

target_link_libraries(
  qinglongclaw
  PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    Threads::Threads
)

target_compile_definitions(qinglongclaw PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(qinglongclaw PRIVATE stdc++fs)
endif()

if(MSVC)
  target_compile_options(qinglongclaw PRIVATE /W4)
else()
  target_compile_options(qinglongclaw PRIVATE -Wall -Wextra -Wpedantic)
endif()

# GCC 10.x can hit internal compiler errors (ICE) on multiple translation units
# with heavy templates (json.hpp/httplib), so we force a conservative build mode.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.0)
  message(STATUS "Applying GCC<11 ICE workarounds (global conservative flags)")
  target_compile_options(
    qinglongclaw
    PRIVATE
      $<$<CONFIG:Release>:-O0;-fno-var-tracking;-fno-var-tracking-assignments;-fno-inline-functions;-fno-tree-vrp;-fno-ipa-cp;-fno-ipa-cp-clone>
  )
  set_source_files_properties(
    src/provider.cpp
    src/gateway.cpp
    PROPERTIES
      COMPILE_OPTIONS "-O0;-fno-var-tracking;-fno-var-tracking-assignments;-fno-inline-functions;-fno-tree-vrp;-fno-ipa-cp;-fno-ipa-cp-clone"
  )
endif()

install(TARGETS qinglongclaw RUNTIME DESTINATION bin)
