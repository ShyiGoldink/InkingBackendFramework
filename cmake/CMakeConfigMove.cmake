if(NOT DEFINED SOURCE_CONFIG_DIR)
    message(FATAL_ERROR "SOURCE_CONFIG_DIR is not defined.")
endif()

if(NOT DEFINED TARGET_CONFIG_DIR)
    message(FATAL_ERROR "TARGET_CONFIG_DIR is not defined.")
endif()

if(EXISTS "${TARGET_CONFIG_DIR}")
    message(STATUS "Config directory already exists, skip copy: ${TARGET_CONFIG_DIR}")
    return()
endif()

file(MAKE_DIRECTORY "${TARGET_CONFIG_DIR}")

if(EXISTS "${SOURCE_CONFIG_DIR}/databaseConfig.json")
    file(COPY
        "${SOURCE_CONFIG_DIR}/databaseConfig.json"
        DESTINATION "${TARGET_CONFIG_DIR}"
    )
else()
    message(FATAL_ERROR "Missing config file: ${SOURCE_CONFIG_DIR}/databaseConfig.json")
endif()

message(STATUS "Config directory copied to: ${TARGET_CONFIG_DIR}")
