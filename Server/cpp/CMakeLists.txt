cmake_minimum_required(VERSION 3.16)
project(ogni_servers CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Прод-цель проекта: Win64 / MSVC v143 /arch:AVX2 RelWithDebInfo (см. SERVER_ARCHITECTURE.md).
# POSIX-сборка (Linux) используется в CI для smoke-прогонов связки; поведение идентично.
if(MSVC)
  add_compile_options(/W3 /permissive- /arch:AVX2)
else()
  add_compile_options(-Wall -O2)
endif()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# MariaDB/MySQL client
find_path(MARIADB_INCLUDE_DIR mysql.h PATHS /usr/include/mariadb /usr/local/include /usr/include)
find_library(MARIADB_LIBRARY NAMES mariadbclient mysqlclient libmariadb PATHS /usr/lib/x86_64-linux-gnu /usr/local/lib)
if(NOT MARIADB_INCLUDE_DIR OR NOT MARIADB_LIBRARY)
  message(FATAL_ERROR "MariaDB/MySQL client library not found (install libmariadb-dev)")
endif()

# libargon2
find_path(ARGON2_INCLUDE_DIR argon2.h)
find_library(ARGON2_LIBRARY NAMES argon2)
if(NOT ARGON2_INCLUDE_DIR OR NOT ARGON2_LIBRARY)
  message(FATAL_ERROR "libargon2 not found (install libargon2-dev)")
endif()

add_library(ogni_common INTERFACE)
target_include_directories(ogni_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${MARIADB_INCLUDE_DIR} ${ARGON2_INCLUDE_DIR})
target_link_libraries(ogni_common INTERFACE ${MARIADB_LIBRARY} ${ARGON2_LIBRARY} OpenSSL::Crypto Threads::Threads)
if(WIN32)
  target_link_libraries(ogni_common INTERFACE ws2_32)
endif()

add_executable(ogni-authserver auth/main.cpp)
target_link_libraries(ogni-authserver PRIVATE ogni_common)

add_executable(ogni-worldserver world/main.cpp)
target_link_libraries(ogni-worldserver PRIVATE ogni_common)

install(TARGETS ogni-authserver ogni-worldserver RUNTIME DESTINATION bin)
