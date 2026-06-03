cmake_minimum_required(VERSION 3.5)

project(ezremote-server)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gdwarf-2 -O0 -pedantic -pedantic-errors")

set(APP_VERSION "1.06")

include_directories(
  source
  source/pugixml
)

add_executable(${PROJECT_NAME}.elf
  source/http/httplib.cpp
  source/server/http_server.cpp
  source/clients/archiveorg.cpp
  source/clients/baseclient.cpp
  source/clients/ftpclient.cpp
  source/clients/sftpclient.cpp
  source/clients/nfsclient.cpp
  source/clients/smbclient.cpp
  source/clients/webdav.cpp
  source/config.cpp
  source/crypt.c
  source/dbglogger.c
  source/fs.cpp
  source/main.cpp
)

target_compile_definitions(${PROJECT_NAME}.elf PRIVATE EZREMOTE_VERSION=${APP_VERSION} CPPHTTPLIB_THREAD_POOL_COUNT=64)

target_link_libraries(${PROJECT_NAME}.elf
  zstd
  z
  psl
  httpclient
  crypto
  ssl
  curl
  json-c
  smb2
  ssh2
  nfs
  SceNotification
  SceSystemService
  SceSysCore
  kernel_sys
  SceAppInstUtil
)
