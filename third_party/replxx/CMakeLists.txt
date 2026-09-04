cmake_minimum_required(VERSION 3.20)

set(REPLXX_SOURCES
    src/ConvertUTF.cpp
    src/conversion.cxx
    src/escape.cxx
    src/history.cxx
    src/prompt.cxx
    src/replxx.cxx
    src/replxx_impl.cxx
    src/terminal.cxx
    src/util.cxx
    src/wcwidth.cpp
)

if(WIN32)
    list(APPEND REPLXX_SOURCES src/windows.cxx)
endif()

if(BUILD_SHARED_LIBS)
    set(REPLXX_LIBRARY_TYPE SHARED)
else()
    set(REPLXX_LIBRARY_TYPE STATIC)
endif()

add_library(replxx ${REPLXX_LIBRARY_TYPE} ${REPLXX_SOURCES})
add_library(replxx::replxx ALIAS replxx)

target_include_directories(replxx
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(replxx PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if(WIN32 AND BUILD_SHARED_LIBS)
    target_compile_definitions(replxx PRIVATE REPLXX_BUILDING_DLL)
endif()

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(replxx PUBLIC REPLXX_STATIC)
endif()
