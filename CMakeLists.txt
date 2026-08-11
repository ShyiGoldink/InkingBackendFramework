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
#目标一：生成名为InkingBackendFramework的可执行文件
#源代码在：src/main.cpp
add_executable(
	InkingBackendFramework
	src/main.cpp
	src/core/Application.cpp
	src/command/CommandCenter.cpp
	src/command/CommandExecutor.cpp
	src/command/CommandLibrary.cpp
	src/command/CommandRegistrant.cpp
    src/database/DatabaseManager.cpp
    src/database/MySQL/MySQLDatabase.cpp
	src/tool/FuzzyMatchTool.cpp
	src/tool/UIRegisterTool.cpp
	src/tool/PasswordTool.cpp
	src/tool/JsonTool.cpp
    src/tool/PathFindTool.cpp
	src/ui/UIThread.cpp
	basic/ShineBasicModule.cpp
	basic/ShineLog.cpp
	basic/ShineStatusChecker.cpp
)
#include目录
target_include_directories(
	InkingBackendFramework
	PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

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
#是否开启使用MySQL
#如果你使用的不是MySQL这里的ON要改成OFF
option(INKING_ENABLE_MYSQL "Enable MySQL database backend" ON)
#如果开启使用MySQL，就要去找是否有本地合适的环境
if(INKING_ENABLE_MYSQL)
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
