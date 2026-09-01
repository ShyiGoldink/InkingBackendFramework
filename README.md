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

MySQL is currently the only built-in database backend, and its sources are always compiled. CMake will try to find the official MySQL C API client through `mysql_config`, then find the MySQL client headers and library from common system paths.

> Note: `-DINKING_ENABLE_MYSQL=OFF` is reserved for future use and does not actually skip the MySQL backend yet. If you want to use another database, implement `IDatabase`, replace `MySQLDatabase` with your own class in `include/database/DatabaseManager.h`, and remove `src/database/MySQL/MySQLDatabase.cpp` from the sources in `CMakeLists.txt`.

If MySQL is installed in a custom location, create a local file named `cmake/CMakeUserPaths.cmake`.
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

## About replxx

This project vendors the third-party library `replxx` under `third_party/replxx` for interactive command-line input. It is a portable readline-style terminal input library, mainly used to build a REPL / console command prompt with UTF-8 support, keyboard editing, command history, and completion hints.

*Why it is included*

- It is bundled in the repository so the project can compile without external package installation.
- The project adds `third_party/replxx/include` to the include path in `CMakeLists.txt`.
- It is intended for terminal-based interactive commands, especially the backend command system and developer CLI experience.

*Current integration pattern*

The build configuration adds the header directory like this:

```cmake
target_include_directories(
    InkingBackendFramework
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/replxx/include
)
```

The library can then be used in C++ code like this:

```cpp
#include "replxx.h"

Replxx* rx = replxx_init();
const char* line = replxx_input(rx, "Inking> ");
if (line != nullptr) {
    // process user input here
}
replxx_end(rx);
```

### Typical capabilities

`replxx` provides common terminal editing features such as:

- UTF-8 text input
- cursor movement
- line editing and deletion
- command history
- completion callbacks
- syntax highlighting / hint callbacks
- cross-platform terminal behavior on Linux, macOS, and Windows

### Notes

- This library is designed for interactive TTY terminals, not for batch or non-console programs.
- In CI, scripts, or non-terminal environments, the input loop may not work as expected.
- The project keeps the upstream source under `third_party/replxx`; if you need to upgrade or modify behavior, edit the vendored code under that directory.
- The upstream library is distributed under a permissive BSD-style license; the project respects the original vendor license in `third_party/replxx/LICENSE.md`.

## Json

This project vendors nlohmann/json under the MIT License.
See `third_party/nlohmann_json/LICENSE.MIT`.
The default database config is `config/databaseConfig.json`. During the first build, CMake copies it to `build/bin/config/databaseConfig.json`.
