# 本文件是 protobuf 源码中 cmake/upb_generators.cmake 的替换版本，
# 由 cmake/PatchProtobufBundled.cmake 写入，内容刻意留空。
#
# 上游在这个文件里定义三个代码生成器可执行文件：
#   protoc-gen-upb / protoc-gen-upbdefs / protoc-gen-upb_minitable
# 本工程只用 protoc 生成 C++ 代码（--cpp_out），从不调用它们；
# 而它们每个都是几十 MB 的静态链接产物，编译加链接既花时间又吃内存
# （首次构建把 16GB 内存打满的那一步里就有它们）。
#
# 上游把它们写死在 protobuf_BUILD_LIBUPB 分支里，没有单独的开关，
# 而 libprotoc 又必须链 libupb（见上游 cmake/libprotoc.cmake），
# 所以不能整个关掉 upb —— 只能把这个文件换成空实现，单独摘掉这三个可执行文件。
#
# 除这三个目标外什么都不改：libupb、libprotobuf、libprotoc、protoc 全部照旧。
