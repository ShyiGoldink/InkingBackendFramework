if(NOT DEFINED SOURCE_CONFIG_DIR)
    message(FATAL_ERROR "SOURCE_CONFIG_DIR is not defined.")
endif()

if(NOT DEFINED TARGET_CONFIG_DIR)
    message(FATAL_ERROR "TARGET_CONFIG_DIR is not defined.")
endif()

file(MAKE_DIRECTORY "${TARGET_CONFIG_DIR}")

# 逐个文件判断，而不是「目标目录存在就整体跳过」：
# 后加的配置文件（例如 networkConfig.json）在已有构建目录里也要能补上，
# 否则老构建目录会永远缺这一份，程序只能默默走默认值。
set(INKING_CONFIG_FILES
    databaseConfig.json
    networkConfig.json
)

foreach(config_file IN LISTS INKING_CONFIG_FILES)
    if(NOT EXISTS "${SOURCE_CONFIG_DIR}/${config_file}")
        message(FATAL_ERROR "Missing config file: ${SOURCE_CONFIG_DIR}/${config_file}")
    endif()

    if(EXISTS "${TARGET_CONFIG_DIR}/${config_file}")
        message(STATUS "Config file already exists, skip copy: ${TARGET_CONFIG_DIR}/${config_file}")
    else()
        file(COPY
            "${SOURCE_CONFIG_DIR}/${config_file}"
            DESTINATION "${TARGET_CONFIG_DIR}"
        )
        message(STATUS "Config file copied to: ${TARGET_CONFIG_DIR}/${config_file}")
    endif()
endforeach()

message(STATUS "Config directory ready: ${TARGET_CONFIG_DIR}")
