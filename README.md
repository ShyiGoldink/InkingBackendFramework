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

## About TCP（鸿蒙硬件文本协议）

面向鸿蒙硬件（BearPi/Hi3861）的 TCP 文本服务。数据库部署到云服务器后才启用
MySQL；本地没有数据库时自动回退内存假数据，因此本地也可以直接联调。

### 协议约定

- 默认监听端口：`8888`（`include/net/TcpServer.h` 中的 `kDefaultTcpPort`，可用 `net-port` 修改）。
- 编码：UTF-8 文本。
- 消息边界：一条消息以 `\n` 结尾，用换行符拆开 TCP 粘包；消息内部不允许出现换行符。
- 消息格式：`指令/数据`，按第一个 `/` 切分。

| 客户端发送 | 含义 | 服务器返回 |
| --- | --- | --- |
| `GET_TIME/` | 拉取服务器时间 | `SEND_TIME/<Unix毫秒>` |
| `GET_DATA_MINUTE/` | 分钟表格（1 分钟/点） | `SEND_DATA_TABEL/[10湿度][10温度][起始毫秒]` |
| `GET_DATA_HOUR/` | 小时表格（1 小时/点） | 同上 |
| `GET_DATA_DAY/` | 天表格（1 天/点） | 同上 |
| `SEND_SENSE_DATA/25.50,60.00` | 上报温度,湿度 | 按测试程序约定不回包（数据入库） |

> 各表格固定 10 个点，服务器只返回起始时间戳；客户端按页面粒度
> （分/时/天）自行推导后续点的时间戳。上传命令如需回执，把
> `SenseDataService.cpp` 中的 `kReplyToUpload` 改成 `true` 即可。

### 代码结构

- `net/TcpServer`：TCP 监听、按行拆包、连接管理（一个连接一个线程）。
- `net/IProtocol`：协议接口，一行文本与 `NetMessage` 互转；实现为 `SimpleTextProtocol`。
- `sense/SenseDataService`：协议分发（GET_TIME / GET_DATA_* / SEND_SENSE_DATA）。
- `sense/ISensorDataStore`：数据存取抽象；`MySQLSensorDataStore`（真实表）与
  `FakeSensorDataStore`（本地假数据）两种实现。
- `sql/sense_schema.sql`：MySQL 表结构（`sense_data`）。

### 控制台命令

- `net-start` / `net-stop`：启动 / 停止 TCP 服务器。
- `net-status`：查看端口与连接数。
- `net-port <端口>`：修改端口（需先停止服务器再启动）。
- `net-clients`：查看当前已连接的客户端。
- `sense-info`：查看当前数据后端（MySQL/假数据）与数据量。

### 联调测试

```bash
python D:\HarmonyTest\test_server.py --host 127.0.0.1 --port 8888
```

测试覆盖 `GET_TIME`、三张表格、传感器上报，全部通过会输出 `全部通过`。
