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
	src/tool/FuzzyMatcher.cpp
	src/ui/UiThread.cpp
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
