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
	src/command/CommandRegistrant.cpp
	src/tool/FuzzyMatchTool.cpp
	src/tool/UIRegisterTool.cpp
	src/tool/PasswordTool.cpp
	src/ui/UIThread.cpp
	basic/ShineBasicModule.cpp
	basic/ShineLog.cpp
	basic/ShineStatusChecker.cpp
)

target_include_directories(
	InkingBackendFramework
	PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(
	InkingBackendFramework
	PRIVATE
	cxx_std_20
)
#检查本地的MySQL环境
#是否开启使用MySQL
#如果你使用的不是MySQL这里的ON要改成OFF
option(INKING_ENABLE_MYSQL "Enable MySQL database backend" ON)
#如果开启使用MySQL，就要去找是否有本地合适的环境
if(INKING_ENABLE_MYSQL)
    # 根据平台设置不同的搜索路径
    if(WIN32)
        set(MYSQL_INCLUDE_HINTS
            "C:/Program Files/MySQL/MySQL Server 8.4/include"
            "C:/Program Files/MySQL/include"
        )
        set(MYSQL_LIBRARY_HINTS
            "C:/Program Files/MySQL/MySQL Server 8.4/lib"
            "C:/Program Files/MySQL/lib"
        )
    elseif(APPLE)
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
        "${CMAKE_CURRENT_SOURCE_DIR}/CMakeUserPaths.cmake"
        CACHE FILEPATH "User local dependency path overrides"
    )
    if(EXISTS "${INKING_USER_PATHS_FILE}")
        include("${INKING_USER_PATHS_FILE}")
    endif()

    # 找include文件
    find_path(MYSQL_INCLUDE_DIR
        NAMES mysql/mysql.h
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
            "If you do not need MySQL, configure with:\n"
            "  cmake -S . -B build -DINKING_ENABLE_MYSQL=OFF\n"
            "Or create CMakeUserPaths.cmake next to CMakeLists.txt:\n"
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

    target_link_libraries(
        InkingBackendFramework
        PRIVATE
        ${MYSQL_LIBRARY}
    )

    target_compile_definitions(
        InkingBackendFramework
        PRIVATE
        INKING_ENABLE_MYSQL
    )

    message(STATUS "MySQL include dir: ${MYSQL_INCLUDE_DIR}")
    message(STATUS "MySQL library: ${MYSQL_LIBRARY}")
endif()
