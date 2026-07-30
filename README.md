# InkingBackendFramework

InkingBackendFramework is the backend framework project for Goldink Workroom's Inking backend development.

## Environment

- CMake: 4.1.2
- protobuf compiler: libprotoc 35.1
- Recommended MySQL version: MySQL 8.4 LTS if you will use MySQL
- Minimum supported version: MySQL 8.0

## Encoding

Use UTF-8 for all source files, headers, CMake files, Markdown documents, and generated text files.

Notes:

- Do not use GBK, ANSI, or other platform-specific encodings for project files.
- Keep Chinese comments and documentation in UTF-8 to avoid garbled text across macOS, Windows, Linux, Git, and editors.
- If an editor asks for "Unicode", choose "UTF-8" explicitly.

## About Database

MySQL support is enabled by default. CMake will try to find the official MySQL C API client through `mysql_config`, then find the MySQL client headers and library from common system paths.

If you do not need MySQL, disable it when configuring the project:

```bash
cmake -S . -B build -DINKING_ENABLE_MYSQL=OFF
```

If MySQL is installed in a custom location, create a local file named `CMakeUserPaths.cmake` next to `CMakeLists.txt`.
This file is ignored by Git and is only used for your local machine.

Example:

```cmake
list(APPEND MYSQL_CONFIG_HINTS "/your/mysql/bin")
list(APPEND MYSQL_INCLUDE_HINTS "/your/mysql/include")
list(APPEND MYSQL_LIBRARY_HINTS "/your/mysql/lib")
```

You can also pass the final paths directly:

```bash
cmake -S . -B build \
  -DMYSQL_INCLUDE_DIR=/your/mysql/include \
  -DMYSQL_LIBRARY=/your/mysql/lib/libmysqlclient.dylib
```

## Json

This project vendors nlohmann/json under the MIT License.
See `third_party/nlohmann_json/LICENSE.MIT`.
Use config/databaseConfig.example.json to achieve your own databaseConfig.json to set your database config.Uh,some words waste your time,sorry.
