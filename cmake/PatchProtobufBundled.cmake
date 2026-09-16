# 让 protobuf 源码使用它自带的 third_party/utf8_range 一起编译，
# 而不是要求机器上已经安装 utf8_range 这个 CMake 包。
#
# 由 CMakeLists.txt 通过 FetchContent 的 PATCH_COMMAND 自动调用（工作目录就是 protobuf 源码目录），
# 也可以手动对一份源码执行：
#   cd <protobuf 源码目录>
#   cmake -P <本工程>/cmake/PatchProtobufBundled.cmake

if(NOT EXISTS "CMakeLists.txt" OR NOT EXISTS "cmake/utf8_range.cmake")
    message(FATAL_ERROR
        "当前目录看起来不是 protobuf 源码目录（缺少 CMakeLists.txt 或 cmake/utf8_range.cmake）。\n"
        "当前目录：${CMAKE_CURRENT_LIST_DIR}")
endif()

# 1) CMakeLists.txt：把必需的 utf8_range 查找改为可选（真正使用内置源码的是下一步替换的文件）
file(READ "CMakeLists.txt" INKING_PROTOBUF_CMAKE)
string(REPLACE
    "find_package(utf8_range CONFIG REQUIRED)"
    "find_package(utf8_range CONFIG QUIET)"
    INKING_PROTOBUF_CMAKE_PATCHED
    "${INKING_PROTOBUF_CMAKE}")
if(NOT INKING_PROTOBUF_CMAKE STREQUAL INKING_PROTOBUF_CMAKE_PATCHED)
    file(WRITE "CMakeLists.txt" "${INKING_PROTOBUF_CMAKE_PATCHED}")
    message(STATUS "protobuf 补丁：CMakeLists.txt 中的 utf8_range 查找改为可选")
endif()

# 2) cmake/utf8_range.cmake：替换为使用内置 third_party/utf8_range 的版本
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/patches/protobuf-utf8_range.cmake"
    "cmake/utf8_range.cmake"
    COPYONLY)
message(STATUS "protobuf 补丁：cmake/utf8_range.cmake 改为编译内置 utf8_range 源码")
