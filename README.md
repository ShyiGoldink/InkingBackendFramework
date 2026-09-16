# InkingBackendFramework

Backend framework for Goldink Workroom's Inking backend development. C++20, CMake, cross-platform (Windows / macOS / Linux).

> **Working on this repository as an AI agent?** Start with [Machine readiness check](#2-machine-readiness-check).
> It tells you what this machine is missing, which version is required, what has to be installed, where the files
> have to end up, and which CMake variables to set. Everything after that is reference material.

Contents

1. [Dependency map](#1-dependency-map)
2. [Machine readiness check](#2-machine-readiness-check)
3. [protobuf](#3-protobuf)
4. [MySQL client (default OFF)](#4-mysql-client-default-off)
5. [Bundled third-party libraries](#5-bundled-third-party-libraries)
6. [Build and run](#6-build-and-run)
7. [Protocol: frame format and messages](#7-protocol-frame-format-and-messages)
8. [Troubleshooting](#8-troubleshooting)
9. [Encoding rules](#9-encoding-rules)
10. [Repository layout](#10-repository-layout)

## 1. Dependency map

| Dependency | Required | Version rule | How the build gets it |
| --- | --- | --- | --- |
| CMake | always | ≥ 3.20 (4.1.2 in use) | `PATH` |
| C++20 compiler | always | MSVC 2022 / GCC 14 / AppleClang 15 verified | `PATH` |
| Ninja | optional | any recent | `PATH`; omit `-G Ninja` to use the default generator |
| git | when protobuf has to be downloaded | any | `PATH` |
| protobuf | `INKING_ENABLE_PROTOBUF=ON` (default) | installed copy: any version, runtime and `protoc` must match; downloaded copy: `INKING_PROTOBUF_TAG`, default `v33.4` | installed copy first, otherwise cloned and built with the project |
| MySQL client development files | `INKING_ENABLE_MYSQL=ON` (default OFF) | 8.4 LTS recommended, 8.0 minimum | `mysql_config`, then header/library search |
| replxx, nlohmann/json | always | vendored | `third_party/`, nothing to install |

Toolchain verified locally: MSYS2 UCRT64 GCC 14.2 + Ninja + CMake on Windows, with protobuf built from the
downloaded source (`INKING_PROTOBUF_SOURCE=auto`, no protobuf installed on the machine).

## 2. Machine readiness check

Run these before configuring; do not guess what is installed.

```bash
cmake --version
c++ --version          # or: g++ --version / clang++ --version / cl
git --version          # needed only when protobuf has to be downloaded
protoc --version       # optional: only matters if you want to use your own protobuf
mysql_config --version # only matters when MySQL is enabled
```

| What you observe | What it means | What to do |
| --- | --- | --- |
| protobuf is missing (no `protoc`, no headers, no library) | nothing to fix for a normal build | the default build downloads protobuf and builds it with the project — [section 3.1](#31-what-the-default-build-does) |
| `protoc` exists but there is no `include/google/protobuf/...` and no runtime library | only the compiler is installed (typical for the release zip) | keep the default, or install the full package to use your own copy — [section 3.4](#34-install-your-own-protobuf) |
| headers + runtime library + `protoc` all present | protobuf is usable as a system dependency | nothing, the default build picks it up |
| `mysql_config` not found | MySQL client development files are missing | [section 4](#4-mysql-client-default-off), or keep `INKING_ENABLE_MYSQL=OFF` |

## 3. protobuf

### 3.1 What the default build does

The default is `INKING_PROTOBUF_SOURCE=auto`:

1. it looks for a protobuf installed on the machine (`find_package(Protobuf CONFIG)`, then CMake's `FindProtobuf`
   module) and uses it — whichever version you installed;
2. if there is none, it clones the protobuf source for `INKING_PROTOBUF_TAG` (default `v33.4`) into
   `build/_deps/protobuf-src`, applies the small patch in `cmake/PatchProtobufBundled.cmake` (so protobuf compiles
   the `utf8_range` sources shipped inside its own tree instead of requiring an installed `utf8_range` package) and
   builds it together with this project. Abseil, which protobuf needs, is fetched and built the same way by
   protobuf's own CMake code.

So a machine without protobuf still builds. The first configure needs network access, and the download goes through
`git`, so your existing proxy/credential configuration applies. Nothing is written into the source tree: both the
cloned sources and the build output stay under `build/`.

### 3.2 Choosing where protobuf comes from

| `-DINKING_PROTOBUF_SOURCE=` | Behaviour |
| --- | --- |
| `auto` (default) | use an installed protobuf if there is one, otherwise download and build |
| `system` | only use an installed protobuf; if there is none, stop with installation instructions |
| `fetch` | always download and build, ignore whatever is installed |

Version used when downloading: `-DINKING_PROTOBUF_TAG=v34.0` (any protobuf release tag).

### 3.3 What "installed" means

If you want the build to use a protobuf you installed yourself, it has to be a prefix directory of this shape:

```text
<protobuf-prefix>/
├── include/google/protobuf/...   # C++ headers
├── lib/libprotobuf.*             # runtime library (libprotobuf.lib on Windows/MSVC)
└── bin/protoc                    # compiler (protoc.exe on Windows)
```

`protoc` alone is not enough: without the headers and the runtime library nothing can be compiled.
The runtime and `protoc` must come from the same version.

### 3.4 Install your own protobuf

| Platform / toolchain | Command | Resulting prefix |
| --- | --- | --- |
| Windows, MSYS2 UCRT64 | `pacman -S mingw-w64-ucrt-x86_64-protobuf` | `C:\msys64\ucrt64` |
| Windows, MSVC | `vcpkg install protobuf:x64-windows` | `D:\vcpkg\vcpkg\installed\x64-windows` |
| macOS (Homebrew) | `brew install protobuf` | `/opt/homebrew` (arm64) or `/usr/local` (x86_64) |
| Ubuntu / Debian | `apt install libprotobuf-dev protobuf-compiler` | `/usr` |

When the prefix is already inside the compiler/SDK search path (normally the case for all four), no extra
configuration is needed. Otherwise create `cmake/CMakeUserPaths.cmake`, which is git-ignored and local to your
machine:

```cmake
# protobuf
set(Protobuf_ROOT "D:/protobuf")

# MySQL (same file; only needed when MySQL is enabled)
list(APPEND MYSQL_CONFIG_HINTS  "D:/mysql/bin")
list(APPEND MYSQL_INCLUDE_HINTS "D:/mysql/include")
list(APPEND MYSQL_LIBRARY_HINTS "D:/mysql/lib")
```

Equivalent command-line flags:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DProtobuf_ROOT=D:/protobuf

# or point at the three pieces directly
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DProtobuf_INCLUDE_DIR=D:/protobuf/include \
  -DProtobuf_LIBRARY=D:/protobuf/lib/libprotobuf.lib \
  -DProtobuf_PROTOC_EXECUTABLE=D:/protobuf/bin/protoc.exe
```

> **Trap — the release zip is not enough.** The official `protoc-<version>-win64.zip` on GitHub contains only
> `protoc` plus the well-known `.proto` files. It ships neither C++ headers nor the runtime library. Use a package
> manager or a source build if you want your own protobuf.

> **Trap — the ABI has to match on Windows.** MSYS2 UCRT64 protobuf is GCC/MinGW-ABI, vcpkg `x64-windows` protobuf
> is MSVC-ABI. They are not interchangeable. Install the one that matches the compiler you configure with.

### 3.5 Verify

Configure prints where protobuf came from and what was picked:

```text
-- protobuf 来源：本机已安装                          # or: 下载 v33.4 源码并随工程一起编译
-- protobuf include dirs: ...
-- protobuf library: protobuf::libprotobuf
-- protoc: ...
-- protobuf version: 33.4
```

Compare with `protoc --version` (it prints `libprotoc 33.4`, i.e. `<minor>.<patch>`; the library version is written
as `<major>.<minor>.<patch>`). A mismatch shows up later as a compile error in the generated code.

### 3.6 Turn it off

```bash
cmake -S . -B build -DINKING_ENABLE_PROTOBUF=OFF
```

With it off, `proto/` is not compiled, no `inking_proto` target is created, and the rest of the project builds
normally.

## 4. MySQL client (default OFF)

### 4.1 What "installed" means

- headers: `mysql.h` (directly or under a `mysql/` subdirectory)
- library: `mysqlclient` / `libmysqlclient` (import library on Windows)
- optional but preferred: `mysql_config` on `PATH`; CMake uses it for include and link flags when available

### 4.2 Install

| Platform | Command | Notes |
| --- | --- | --- |
| Windows | MySQL Installer → MySQL Server 8.4 LTS (includes the client C API files) | CMake already probes `C:/Program Files/MySQL/MySQL Server 8.4/{include,lib}` |
| macOS | `brew install mysql-client` | CMake probes `/opt/homebrew/opt/mysql-client/...` and `/usr/local/...` |
| Ubuntu / Debian | `apt install libmysqlclient-dev` | CMake probes `/usr/include` and `/usr/lib/x86_64-linux-gnu` |

If none of those match, use the `MYSQL_*_HINTS` variables from [section 3.4](#34-install-your-own-protobuf), or pass
`-DMYSQL_INCLUDE_DIR=... -DMYSQL_LIBRARY=...` directly.

### 4.3 Enable

```bash
cmake -S . -B build -DINKING_ENABLE_MYSQL=ON
```

Default is OFF. With it off, `src/database/MySQL/MySQLDatabase.cpp` is not compiled at all and the database
commands report that the backend is unavailable. To use another database, implement `IDatabase` and swap
`MySQLDatabase` in `include/database/DatabaseManager.h`.

### 4.4 Runtime configuration

The connection settings live in `config/databaseConfig.json`:

```json
{
    "databaseConfig": {
        "host": "127.0.0.1",
        "port": 3306,
        "userName": "root",
        "password": "123456",
        "databaseName": "default"
    }
}
```

On the first build CMake copies it to `build/bin/config/databaseConfig.json`. The copy is only created when the
target directory does not exist yet, so from then on the file the program actually reads is the one under
`build/bin/config/` — edit that one for local runs, and it will not be overwritten by later builds.

## 5. Bundled third-party libraries

Nothing to install; both are vendored in `third_party/`.

- `third_party/replxx` — readline-style terminal input for the console REPL: UTF-8 input, cursor movement, line
  editing, command history, completion callbacks. It is compiled by its own CMake target and linked into the
  executable; use it from C++ via `#include "replxx.h"`. It targets interactive TTY terminals, so in CI or
  non-terminal environments the input loop will not behave as expected. Upstream license: `third_party/replxx/LICENSE.md`.
- `third_party/nlohmann_json` — header-only JSON library (MIT), used to read `databaseConfig.json`.
  License: `third_party/nlohmann_json/LICENSE.MIT`.

## 6. Build and run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

./build/bin/InkingBackendFramework            # Windows: build\bin\InkingBackendFramework.exe
```

On a machine without protobuf the first configure downloads and patches the protobuf source (about one minute on the
verified machine), and the first build additionally compiles protobuf and Abseil (a few minutes). Later builds are
incremental.

The executable asks for a console password at startup (five attempts, hash checked in `src/tool/PasswordTool.cpp`)
before it starts the rest of the framework, then drops into the command prompt where `help` lists the available
commands. Running it from a script without an interactive terminal will not get past the password prompt.

Build options: `INKING_ENABLE_PROTOBUF` (default ON), `INKING_PROTOBUF_SOURCE` (default `auto`),
`INKING_PROTOBUF_TAG` (default `v33.4`), `INKING_ENABLE_MYSQL` (default OFF), `CMAKE_BUILD_TYPE`.

## 7. Protocol: frame format and messages

### 7.1 Frame format

Everything that goes over the wire is one frame. All integers are big-endian (network byte order):

```text
+------------------+-------------------+------------------+------------------+
| packet length    | command length    | command (UTF-8)  | payload          |
| 8 bytes, uint64  | 4 bytes, uint32   | command length B | rest of the frame|
+------------------+-------------------+------------------+------------------+
```

- `packet length` is the size of the whole frame, header included;
- `payload length` is derived: `packet length - 12 - command length`;
- the payload is the protobuf-serialised message; the frame layer never looks inside it and does not depend on
  protobuf at all;
- limits: command ≤ 1024 bytes, whole frame ≤ 16 MiB — see `include/protocol/ProtocolFrame.h`.

Implemented in `include/protocol/ProtocolFrame.h`, `include/protocol/FrameCodec.h` and `src/protocol/FrameCodec.cpp`:

- `FrameCodec::encode()` turns a `CommandFrame` into the bytes to send;
- `FrameCodec::push()` + `FrameCodec::next()` turn a received byte stream into frames, handling sticky and partial
  TCP reads (several frames in one read, half a frame per read);
- a header that cannot be valid (length out of range, command length larger than the frame) drops the buffer and
  returns `InvalidFrame`, so one broken message cannot stall everything after it.

### 7.2 Messages and code generation

- Message definitions live in `proto/`, one file per domain, e.g. `proto/framework/Ping.proto`.
- Every `.proto` under `proto/` is compiled automatically with the `protoc` the build provides; `cmake --build`
  runs it, there is no manual generation step.
- Generated sources go to `build/generated/...` and are never committed.
- They are compiled into the static library `inking_proto`, which the executable links, so business code includes
  the generated headers directly: `#include "framework/Ping.pb.h"`.
- Adding a message is just dropping a `.proto` into `proto/`; the file list is re-globbed at configure time
  (`CONFIGURE_DEPENDS`).
- Convention: one data type per message. The "command → message" mapping table will be built on top of protobuf
  descriptors (`DescriptorPool` + `DynamicMessageFactory`), so adding a command means registering it, not writing
  a new branch.

## 8. Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Configure fails while downloading protobuf, mentions SSL / CA certificate or a network error | CMake cannot reach GitHub through git | make `git clone https://github.com/protocolbuffers/protobuf.git` work in your shell (proxy, credentials), or install protobuf and use `-DINKING_PROTOBUF_SOURCE=system` |
| Configure fails with "没有可用的 protobuf" | `INKING_PROTOBUF_SOURCE=system` but nothing is installed | install protobuf ([section 3.4](#34-install-your-own-protobuf)) or use the default `auto` |
| `protoc` runs in the shell but the build does not use it | only the compiler is installed, no headers/runtime | install the full development package, or keep the downloaded protobuf |
| Linker errors mentioning protobuf | protobuf built for a different ABI (MSVC vs MinGW) or a different version | use the protobuf that matches the compiler you configure with |
| Generated `.pb.cc` fails to compile | `protoc` and runtime versions differ | same version for both, or use the downloaded copy |
| Configure fails with "MySQL client library not found" | MySQL development files missing while `INKING_ENABLE_MYSQL=ON` | [section 4.2](#42-install), or turn the option off |
| Changes to `config/databaseConfig.json` have no effect | the program reads the copy under `build/bin/config/` | edit `build/bin/config/databaseConfig.json` |
| Console shows garbled Chinese | console code page or a non-UTF-8 source file | the program sets UTF-8 itself; keep every source file UTF-8 (section 9) |

## 9. Encoding rules

Use UTF-8 for all source files, headers, CMake files, Markdown documents, and generated text files.

Notes:

- Do not use GBK, ANSI, or other platform-specific encodings for project files.
- Keep Chinese comments and documentation in UTF-8 to avoid garbled text across macOS, Windows, Linux, Git, and editors.
- If an editor asks for "Unicode", choose "UTF-8" explicitly.

## 10. Repository layout

```text
basic/         Base classes: self-check stages, logging, module registry (ShineBasicModule / ShineLog / ShineStatusChecker)
include/       Public headers, grouped by domain: command/ database/ protocol/ thread/ tool/ ui/
src/           Implementations, mirroring include/
proto/         protobuf message definitions (C++ is generated from these; generated files are not committed)
third_party/   Vendored libraries: replxx, nlohmann_json
config/        Runtime configuration template (databaseConfig.json)
cmake/         CMake helper scripts, patches/ for protobuf, CMakeUserPaths.cmake for local path overrides (git-ignored)
build/         Build output, including the downloaded protobuf source under build/_deps/ (git-ignored)
```
