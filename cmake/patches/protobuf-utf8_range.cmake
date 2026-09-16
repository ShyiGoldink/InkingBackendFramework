# 本文件是 protobuf 源码中 cmake/utf8_range.cmake 的替换版本，由 PatchProtobufBundled.cmake 写入。
#
# 上游把这个「使用内置 third_party/utf8_range」的分支写死成 if (0)，
# 改为在目标不存在时用 add_subdirectory 编译内置源码，
# 并补上 libprotobuf / libprotobuf-lite 需要的命名空间目标。
if (NOT TARGET utf8_range::utf8_validity)
  set(utf8_range_ENABLE_TESTS OFF CACHE BOOL "Disable utf8_range tests")

  if (NOT EXISTS "${protobuf_SOURCE_DIR}/third_party/utf8_range/CMakeLists.txt")
    message(FATAL_ERROR
            "Cannot find third_party/utf8_range directory that's needed for "
            "the protobuf runtime.\n")
  endif()

  set(utf8_range_ENABLE_INSTALL ${protobuf_INSTALL} CACHE BOOL "Set install")
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/utf8_range third_party/utf8_range)
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/utf8_range)

  add_library(utf8_range::utf8_range ALIAS utf8_range)
  add_library(utf8_range::utf8_validity ALIAS utf8_validity)
endif ()

set(_protobuf_FIND_UTF8_RANGE "if(NOT TARGET utf8_range::utf8_range)\n  find_package(utf8_range CONFIG)\nendif()")
