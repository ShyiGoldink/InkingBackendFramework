# cmake_minimum_required  要求 CMake 最低版本
# project                 定义项目
# set                     设置变量
# add_executable          生成可执行程序
# add_library             生成库
# target_include_directories  给某个目标加头文件路径
# target_link_libraries       给某个目标链接库
# target_compile_features     指定 C++ 标准/特性
# target_compile_definitions  添加宏定义
# find_package            找外部库，比如 Qt/protobuf/OpenSSL
#
#
#
#本项目由Shyi组建，用于Goldink Workroom的后端开发框架



# cmake最小支持版本，告知解析程序，避免旧版本CMake不认识新语法
cmake_minimum_required(VERSION 3.20)
#定义项目名称和语言，CXX代表C++
project(InkingBackendFramework LANGUAGES CXX)
#使用C++20的标准
set(CMAKE_CXX_STANDARD 20)
#设置项目输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

#添加第三方库支持
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/replxx)

# MySQL 开关：关闭时不编译 MySQL 后端
option(INKING_ENABLE_MYSQL "Enable MySQL database backend" OFF)

# ------------------------------------------------------------------
# protobuf 开关
# 先使用机器上已经安装的 protobuf（版本由使用者决定，运行库与 protoc 同版本）；
# 找不到时，CMake 自动按 INKING_PROTOBUF_TAG 下载 protobuf 源码，
# 和本工程一起编译（Abseil 由 protobuf 自己拉取，补丁见 cmake/PatchProtobufBundled.cmake）。
# 源码只落在构建目录，不进仓库。
# ------------------------------------------------------------------
option(INKING_ENABLE_PROTOBUF "Enable protobuf protocol layer" ON)
set(INKING_PROTOBUF_SOURCE "auto" CACHE STRING
    "protobuf 来源：auto = 先用本机已安装的，没有就下载源码一起编译；system = 只用本机已安装的；fetch = 总是下载源码")
set_property(CACHE INKING_PROTOBUF_SOURCE PROPERTY STRINGS auto system fetch)
set(INKING_PROTOBUF_TAG "v33.4" CACHE STRING
    "下载 protobuf 源码时使用的版本标签（github tag），例如 v33.4")

if(INKING_ENABLE_PROTOBUF)
    # 本地路径覆盖与 MySQL 共用 cmake/CMakeUserPaths.cmake，例如：
    #   set(Protobuf_ROOT "/your/protobuf")
    #   list(APPEND CMAKE_PREFIX_PATH "/your/protobuf")
    set(INKING_USER_PATHS_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeUserPaths.cmake"
        CACHE FILEPATH "User local dependency path overrides"
    )
    if(EXISTS "${INKING_USER_PATHS_FILE}")
        include("${INKING_USER_PATHS_FILE}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/CMakeUserPaths.cmake")
        include("${CMAKE_CURRENT_SOURCE_DIR}/CMakeUserPaths.cmake")
    endif()

    set(INKING_PROTOBUF_READY FALSE)

    # 第一步：机器上已经安装的 protobuf。
    # 优先用官方 CMake 包（自己编译安装或 vcpkg 提供的都是这种），
    # 找不到时退回 CMake 自带的 FindProtobuf 模块。
    if(NOT INKING_PROTOBUF_SOURCE STREQUAL "fetch")
        find_package(Protobuf CONFIG QUIET)
        if(NOT Protobuf_FOUND)
            find_package(Protobuf MODULE QUIET)
        endif()
        if(Protobuf_FOUND)
            set(INKING_PROTOBUF_READY TRUE)
            message(STATUS "protobuf 来源：本机已安装")
        endif()
    endif()

    # 第二步：没有装 protobuf 时，下载源码并和本工程一起编译。
    # 源码与编译产物都只放在构建目录里，不会进入源码树。
    if(NOT INKING_PROTOBUF_READY AND NOT INKING_PROTOBUF_SOURCE STREQUAL "system")
        include(FetchContent)
        message(STATUS "protobuf 来源：下载 ${INKING_PROTOBUF_TAG} 源码并随工程一起编译")

        set(protobuf_BUILD_TESTS OFF CACHE BOOL "protobuf: disable tests" FORCE)
        set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "protobuf: disable conformance tests" FORCE)
        set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "protobuf: disable examples" FORCE)
        set(protobuf_INSTALL OFF CACHE BOOL "protobuf: disable install rules" FORCE)
        set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "protobuf: build static libraries" FORCE)
        # libprotoc 会链接 libupb，因此保留 upb 的编译
        set(protobuf_BUILD_LIBUPB ON CACHE BOOL "protobuf: build libupb" FORCE)
        # 保留 protoc，用于把 proto/ 下的 .proto 生成 C++ 代码
        set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "protobuf: build protoc" FORCE)
        set(protobuf_WITH_ZLIB OFF CACHE BOOL "protobuf: build without zlib" FORCE)

        # 用 git 拉取而不是 URL 下载：git 自带证书配置，不依赖 CMake 下载器的 CA 设置，
        # 也方便在内网/代理环境下复用使用者已有的 git 配置。
        FetchContent_Declare(protobuf
            GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
            GIT_TAG ${INKING_PROTOBUF_TAG}
            GIT_SHALLOW TRUE
            PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchProtobufBundled.cmake
        )
        FetchContent_MakeAvailable(protobuf)

        set(INKING_PROTOBUF_READY TRUE)
        set(Protobuf_INCLUDE_DIRS ${protobuf_SOURCE_DIR}/src ${protobuf_BINARY_DIR}/src)
        # 源码版本就是下载时使用的 tag，去掉前缀 v 后与 protoc --version 的写法一致
        string(REGEX REPLACE "^v" "" INKING_PROTOBUF_TAG_VERSION "${INKING_PROTOBUF_TAG}")
        set(Protobuf_VERSION "${INKING_PROTOBUF_TAG_VERSION}")
    endif()

    if(NOT INKING_PROTOBUF_READY)
        message(FATAL_ERROR
            "没有可用的 protobuf。\n"
            "当前 INKING_PROTOBUF_SOURCE=${INKING_PROTOBUF_SOURCE}，只允许使用本机已安装的 protobuf，但没有找到。\n"
            "常见安装方式：\n"
            "  MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-protobuf\n"
            "  macOS:        brew install protobuf\n"
            "  Ubuntu:       apt install libprotobuf-dev protobuf-compiler\n"
            "如果 protobuf 装在自定义目录，创建 cmake/CMakeUserPaths.cmake 后写：\n"
            "  set(Protobuf_ROOT \"/your/protobuf\")\n"
            "也可以改成自动下载源码一起编译：-DINKING_PROTOBUF_SOURCE=auto\n"
            "暂时不需要协议层时，配置时加上：-DINKING_ENABLE_PROTOBUF=OFF\n")
    endif()

    # 统一成两个变量：库目标 + protoc 命令，兼容 CONFIG 与 MODULE 两种查找结果
    if(TARGET protobuf::libprotobuf)
        set(INKING_PROTOBUF_LIBRARY protobuf::libprotobuf)
    else()
        set(INKING_PROTOBUF_LIBRARY ${Protobuf_LIBRARIES})
    endif()

    if(TARGET protobuf::protoc)
        set(INKING_PROTOC_COMMAND $<TARGET_FILE:protobuf::protoc>)
        set(INKING_PROTOC_DEPENDENCY protobuf::protoc)
    else()
        set(INKING_PROTOC_COMMAND ${Protobuf_PROTOC_EXECUTABLE})
        set(INKING_PROTOC_DEPENDENCY ${Protobuf_PROTOC_EXECUTABLE})
    endif()

    message(STATUS "protobuf include dirs: ${Protobuf_INCLUDE_DIRS}")
    message(STATUS "protobuf library: ${INKING_PROTOBUF_LIBRARY}")
    message(STATUS "protoc: ${INKING_PROTOC_COMMAND}")
    if(Protobuf_VERSION)
        message(STATUS "protobuf version: ${Protobuf_VERSION}")
    endif()
endif()

# ------------------------------------------------------------------
# .proto 代码生成
# proto/ 下的全部 .proto 由使用者提供的 protoc 生成 C++ 代码，
# 生成结果只落在构建目录中，不进入源码树。
# ------------------------------------------------------------------
if(INKING_ENABLE_PROTOBUF)
    set(INKING_PROTO_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
    set(INKING_PROTO_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

    file(GLOB_RECURSE INKING_PROTO_FILES CONFIGURE_DEPENDS ${INKING_PROTO_ROOT_DIR}/*.proto)
    if(NOT INKING_PROTO_FILES)
        message(FATAL_ERROR "在 ${INKING_PROTO_ROOT_DIR} 下没有找到任何 .proto 文件")
    endif()

    set(INKING_PROTO_GENERATED_SOURCES)
    foreach(INKING_PROTO_FILE ${INKING_PROTO_FILES})
        file(RELATIVE_PATH INKING_PROTO_RELATIVE ${INKING_PROTO_ROOT_DIR} ${INKING_PROTO_FILE})
        get_filename_component(INKING_PROTO_SUBDIR ${INKING_PROTO_RELATIVE} DIRECTORY)
        get_filename_component(INKING_PROTO_NAME ${INKING_PROTO_FILE} NAME_WE)

        set(INKING_PB_CC ${INKING_PROTO_OUT_DIR}/${INKING_PROTO_SUBDIR}/${INKING_PROTO_NAME}.pb.cc)
        set(INKING_PB_H ${INKING_PROTO_OUT_DIR}/${INKING_PROTO_SUBDIR}/${INKING_PROTO_NAME}.pb.h)

        add_custom_command(
            OUTPUT ${INKING_PB_CC} ${INKING_PB_H}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${INKING_PROTO_OUT_DIR}/${INKING_PROTO_SUBDIR}
            COMMAND ${INKING_PROTOC_COMMAND}
                    --proto_path=${INKING_PROTO_ROOT_DIR}
                    --cpp_out=${INKING_PROTO_OUT_DIR}
                    ${INKING_PROTO_FILE}
            DEPENDS ${INKING_PROTO_FILE} ${INKING_PROTOC_DEPENDENCY}
            COMMENT "生成 protobuf 代码: ${INKING_PROTO_RELATIVE}"
            VERBATIM
        )
        list(APPEND INKING_PROTO_GENERATED_SOURCES ${INKING_PB_CC})
    endforeach()

    # 生成代码单独编成一个静态库，业务代码通过链接它来使用消息类型
    add_library(inking_proto STATIC ${INKING_PROTO_GENERATED_SOURCES})
    target_include_directories(inking_proto PUBLIC ${INKING_PROTO_OUT_DIR} ${Protobuf_INCLUDE_DIRS})
    target_link_libraries(inking_proto PUBLIC ${INKING_PROTOBUF_LIBRARY})
endif()

#变量声明：源代码在：src/main.cpp等
set(INKING_SOURCES
    src/main.cpp
    src/core/Application.cpp
    src/command/CommandCenter.cpp
    src/command/CommandExecutor.cpp
    src/command/CommandLibrary/CommandLibrary.cpp
    src/command/CommandLibrary/CommandLibraryTagDatabase.cpp
    src/command/CommandLibrary/CommandLibraryTagUI.cpp
    src/command/CommandRegistrant.cpp
    src/database/DatabaseManager.cpp
    src/database/DatabasePool.cpp
    src/protocol/FrameCodec.cpp
    src/tool/FuzzyMatchTool.cpp
    src/tool/PasswordTool.cpp
    src/tool/JsonTool.cpp
    src/tool/PathFindTool.cpp
    src/ui/UIThread.cpp
    src/ui/UIMessageLibrary.cpp
    src/thread/ThreadPool.cpp
    src/thread/TaskQueueLoop.cpp
    basic/ShineBasicModule.cpp
    basic/ShineLog.cpp
    basic/ShineStatusChecker.cpp
)

if(INKING_ENABLE_MYSQL)
    list(APPEND INKING_SOURCES src/database/MySQL/MySQLDatabase.cpp)
endif()

#目标一：生成名为InkingBackendFramework的可执行文件
add_executable(
	InkingBackendFramework
	${INKING_SOURCES}
)
# include目录
# replxx 由独立 CMake target 负责编译和链接，确保跨平台下都能得到正确的库定义

target_include_directories(
	InkingBackendFramework
	PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    InkingBackendFramework
    PRIVATE
        replxx
)

if(TARGET inking_proto)
    target_link_libraries(
        InkingBackendFramework
        PRIVATE
            inking_proto
    )
endif()

target_compile_features(
	InkingBackendFramework
	PRIVATE
	cxx_std_20
)

# 构建完成后，首次复制默认配置到可执行文件同级目录
add_custom_command(
    TARGET InkingBackendFramework
    POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_CONFIG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/config
        -DTARGET_CONFIG_DIR=$<TARGET_FILE_DIR:InkingBackendFramework>/config
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeConfigMove.cmake
)

#检查本地的MySQL环境
#如果开启使用MySQL，就要去找是否有本地合适的环境
if(INKING_ENABLE_MYSQL)
    target_compile_definitions(InkingBackendFramework PRIVATE INKING_ENABLE_MYSQL=1)
    # 根据平台设置不同的搜索路径
    if(WIN32)
        set(MYSQL_CONFIG_HINTS)
        set(MYSQL_INCLUDE_HINTS
            "C:/Program Files/MySQL/MySQL Server 8.4/include"
            "C:/Program Files/MySQL/include"
        )
        set(MYSQL_LIBRARY_HINTS
            "C:/Program Files/MySQL/MySQL Server 8.4/lib"
            "C:/Program Files/MySQL/lib"
        )
    elseif(APPLE)
        set(MYSQL_CONFIG_HINTS
            /opt/homebrew/bin
            /opt/homebrew/opt/mysql-client/bin
            /usr/local/bin
            /usr/local/opt/mysql-client/bin
            /usr/local/mysql/bin
        )
        set(MYSQL_INCLUDE_HINTS
            /opt/homebrew/include
            /opt/homebrew/opt/mysql-client/include
            /usr/local/include
            /usr/local/opt/mysql-client/include
            /usr/local/mysql/include
        )
        set(MYSQL_LIBRARY_HINTS
            /opt/homebrew/lib
            /opt/homebrew/opt/mysql-client/lib
            /usr/local/lib
            /usr/local/opt/mysql-client/lib
            /usr/local/mysql/lib
        )
    else()  # Linux
        set(MYSQL_CONFIG_HINTS
            /usr/bin
            /usr/local/bin
            /usr/local/mysql/bin
        )
        set(MYSQL_INCLUDE_HINTS
            /usr/include
            /usr/local/include
        )
        set(MYSQL_LIBRARY_HINTS
            /usr/lib
            /usr/local/lib
            /usr/lib/x86_64-linux-gnu
            /usr/local/lib/x86_64-linux-gnu
        )
    endif()

    # 加载用户自定义路径（如果存在）
    set(INKING_USER_PATHS_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeUserPaths.cmake"
        CACHE FILEPATH "User local dependency path overrides"
    )
    if(EXISTS "${INKING_USER_PATHS_FILE}")
        include("${INKING_USER_PATHS_FILE}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/CMakeUserPaths.cmake")
        include("${CMAKE_CURRENT_SOURCE_DIR}/CMakeUserPaths.cmake")
    endif()

    # 优先使用MySQL官方提供的mysql_config获取编译和链接信息
    find_program(MYSQL_CONFIG_EXECUTABLE
        NAMES mysql_config
        PATHS ${MYSQL_CONFIG_HINTS}
    )

    if(MYSQL_CONFIG_EXECUTABLE)
        execute_process(
            COMMAND ${MYSQL_CONFIG_EXECUTABLE} --include
            OUTPUT_VARIABLE MYSQL_CONFIG_INCLUDE_FLAGS
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${MYSQL_CONFIG_EXECUTABLE} --libs
            OUTPUT_VARIABLE MYSQL_CONFIG_LIBRARY_FLAGS
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        separate_arguments(MYSQL_CONFIG_INCLUDE_ARGS UNIX_COMMAND "${MYSQL_CONFIG_INCLUDE_FLAGS}")
        foreach(MYSQL_INCLUDE_ARG ${MYSQL_CONFIG_INCLUDE_ARGS})
            if(MYSQL_INCLUDE_ARG MATCHES "^-I(.+)")
                list(APPEND MYSQL_INCLUDE_HINTS "${CMAKE_MATCH_1}")
            endif()
        endforeach()

        separate_arguments(MYSQL_CONFIG_LIBRARY_ARGS UNIX_COMMAND "${MYSQL_CONFIG_LIBRARY_FLAGS}")
        foreach(MYSQL_LIBRARY_ARG ${MYSQL_CONFIG_LIBRARY_ARGS})
            if(MYSQL_LIBRARY_ARG MATCHES "^-L(.+)")
                list(APPEND MYSQL_LIBRARY_HINTS "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()

    # mysql_config 可能只返回 -lssl/-lcrypto。
    # macOS 下这会让可执行文件记录裸库名，运行时可能找不到 OpenSSL 动态库。
    find_library(INKING_OPENSSL_SSL_LIBRARY
        NAMES ssl libssl
        PATHS
        /opt/homebrew/opt/openssl@3/lib
        /opt/homebrew/lib
        /usr/local/opt/openssl@3/lib
        ${MYSQL_LIBRARY_HINTS}
    )
    find_library(INKING_OPENSSL_CRYPTO_LIBRARY
        NAMES crypto libcrypto
        PATHS
        /opt/homebrew/opt/openssl@3/lib
        /opt/homebrew/lib
        /usr/local/opt/openssl@3/lib
        ${MYSQL_LIBRARY_HINTS}
    )

    set(MYSQL_LINK_ARGS ${MYSQL_CONFIG_LIBRARY_ARGS})
    if(INKING_OPENSSL_SSL_LIBRARY AND INKING_OPENSSL_CRYPTO_LIBRARY)
        list(REMOVE_ITEM MYSQL_LINK_ARGS -lssl -lcrypto)
    endif()

    # 找include文件
    find_path(MYSQL_INCLUDE_DIR
        NAMES mysql.h mysql/mysql.h
        PATHS ${MYSQL_INCLUDE_HINTS}
    )

    # 找动态链接库
    find_library(MYSQL_LIBRARY
        NAMES mysqlclient libmysqlclient
        PATHS ${MYSQL_LIBRARY_HINTS}
    )

    # 如果找不到，就要发送消息
	if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
		message(FATAL_ERROR
			"MySQL client library not found.\n"
            "Recommended version: MySQL 8.4 LTS.\n"
            "Minimum supported version: MySQL 8.0.\n"
            "Please install MySQL 8.4 LTS client development files.\n"
            "CMake will prefer the official mysql_config tool when it is available.\n"
            "If you do not need MySQL, configure with:\n"
            "  cmake -S . -B build -DINKING_ENABLE_MYSQL=OFF\n"
            "Or create cmake/CMakeUserPaths.cmake:\n"
            "  list(APPEND MYSQL_CONFIG_HINTS /path/to/mysql/bin)\n"
            "  list(APPEND MYSQL_INCLUDE_HINTS /path/to/mysql/include)\n"
            "  list(APPEND MYSQL_LIBRARY_HINTS /path/to/mysql/lib)\n"
            "See README to learn more.\n"
			"没有MySQL环境,但是你在CMake中要求了它。\n"
			"如果你已经安装环境或者不打算使用MySQL,请阅读README并按照中的方法进行调试。"
		)
	endif()

    target_include_directories(
        InkingBackendFramework
        PRIVATE
        ${MYSQL_INCLUDE_DIR}
    )

    if(MYSQL_CONFIG_EXECUTABLE)
        target_link_libraries(
            InkingBackendFramework
            PRIVATE
            ${MYSQL_LINK_ARGS}
        )
    else()
        target_link_libraries(
            InkingBackendFramework
            PRIVATE
            ${MYSQL_LIBRARY}
        )
    endif()
    if(INKING_OPENSSL_SSL_LIBRARY AND INKING_OPENSSL_CRYPTO_LIBRARY)
        target_link_libraries(
            InkingBackendFramework
            PRIVATE
            ${INKING_OPENSSL_SSL_LIBRARY}
            ${INKING_OPENSSL_CRYPTO_LIBRARY}
        )
    endif()

    # macOS/Linux 运行时也需要知道动态库目录。
    # -L 只解决链接时查找库，BUILD_RPATH 解决运行时查找 dylib/so。
    set(MYSQL_RUNTIME_LIBRARY_DIRS)
    if(MYSQL_LIBRARY)
        get_filename_component(MYSQL_LIBRARY_DIR "${MYSQL_LIBRARY}" DIRECTORY)
        list(APPEND MYSQL_RUNTIME_LIBRARY_DIRS "${MYSQL_LIBRARY_DIR}")
    endif()
    foreach(MYSQL_LIBRARY_ARG ${MYSQL_CONFIG_LIBRARY_ARGS})
        if(MYSQL_LIBRARY_ARG MATCHES "^-L(.+)")
            list(APPEND MYSQL_RUNTIME_LIBRARY_DIRS "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(INKING_OPENSSL_SSL_LIBRARY)
        get_filename_component(INKING_OPENSSL_SSL_LIBRARY_DIR "${INKING_OPENSSL_SSL_LIBRARY}" DIRECTORY)
        list(APPEND MYSQL_RUNTIME_LIBRARY_DIRS "${INKING_OPENSSL_SSL_LIBRARY_DIR}")
    endif()
    if(INKING_OPENSSL_CRYPTO_LIBRARY)
        get_filename_component(INKING_OPENSSL_CRYPTO_LIBRARY_DIR "${INKING_OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
        list(APPEND MYSQL_RUNTIME_LIBRARY_DIRS "${INKING_OPENSSL_CRYPTO_LIBRARY_DIR}")
    endif()
    if(MYSQL_RUNTIME_LIBRARY_DIRS)
        list(REMOVE_DUPLICATES MYSQL_RUNTIME_LIBRARY_DIRS)
        set_target_properties(
            InkingBackendFramework
            PROPERTIES
            BUILD_RPATH "${MYSQL_RUNTIME_LIBRARY_DIRS}"
        )
    endif()

    target_compile_definitions(
        InkingBackendFramework
        PRIVATE
        INKING_ENABLE_MYSQL
    )

    if(MYSQL_CONFIG_EXECUTABLE)
        message(STATUS "MySQL config: ${MYSQL_CONFIG_EXECUTABLE}")
    endif()
    message(STATUS "MySQL include dir: ${MYSQL_INCLUDE_DIR}")
    message(STATUS "MySQL library: ${MYSQL_LIBRARY}")
endif()
