# 日志系统重构方案（修订版 · 待批准）

> 状态：**已批准，已按 D1–D5 实施完成**（决策：纯文本 / 一次运行一文件 / HTML 按需生成 / 控制台零日志 / 差分快照 5 分钟）
> 初稿：2026-10-21 · 修订：2026-10-21（自检记录模型、控制台策略）· 实施：2026-10-21
> 检查范围：`basic/ShineLog.*`、`basic/ShineBasicModule.*`、全部 `setStageStatus` 调用点、运行期产物 `build/bin/LOG/`

---

## 一、检查结论（含实测证据）

### 1.1 现状结构

日志出口只有 3 个公开函数（`write / pass / error`），共 3 个级别（日志 / 通过 / 错误）。
但真正产生日志的入口不是它们，而是自检机制：

| 入口 | 数量 | 说明 |
|---|---|---|
| `setStageStatus()` 隐式写日志 | 30+ 处 | 每次阶段状态更新必然追加一行 |
| `ShineLog::error()` 显式调用 | 4 处 | 仅异常兜底（ThreadPool / TaskQueueLoop） |

也就是说：**日志量 = 自检频率**，而不是"事件的重要程度"。这是"重复日志淹没一切"的结构性原因。

### 1.2 实测数据（以控制台密码启动，输入 `status`、`exit`）

产物：`build/bin/LOG/2026-10-21/19-34-40.html`

| 指标 | 数值 | 结论 |
|---|---|---|
| 文件大小 | 8756 字节 | — |
| HTML 模板占用 | 7354 字节 / 285 行 | **84% 是模板** |
| 有效日志条目 | 7 条 | 平均 1.25 KB/条 |
| 其中空消息条目 | 1 条（`[Application]` 后面什么都没有） | 见 1.3 缺陷 A |
| 其中完全重复条目 | 2 对（"读取控制台输入""执行控制台命令"各 2 次） | 见 1.4 |
| 记录的"用户输入了什么命令" | **0 条** | 见 1.5，最致命的一条 |
| `</body></html>` 收尾 | **不存在** | 文件不是合法 HTML |

历史样本：`LOG/2026-10-04/` 一天 18 个文件，每个 7.6–13 KB、288–319 行。单条 `[ThreadPool] 向线程中注入管家函数 - 已创建线程管家` 连续出现 **10 次**，一字不差。

### 1.3 已定位的具体缺陷

**缺陷 A：阶段首次创建时丢失状态与消息（`basic/ShineBasicModule.cpp:31-38`）**

```cpp
if (stage == nullptr) {
    Stage newStage;
    newStage.step = step;
    newStage.name = name;
    _stage.push_back(newStage);   // ← 没有写 status / message
    stage = &_stage.back();       // ← 也没有给 logMessage 赋值
}
```

后果有两个，都被实测到了：
1. 首行日志必为空（`[Application]` 后无内容，老日志里 `[Net]` 5 条空行同源）；
2. 控制台 `status` 显示 `[通过] 打印启动提示 - ` 后面空白，因为 `stage->message` 从未被赋值。

**缺陷 B：HTML 深度耦合在写盘热路径（`ShineLog.cpp:39-327, 386-392`）**

- 每次运行都整段重写 CSS（约 200 行）+ 一段 DOM 重排 JS（约 110 行）；
- 只写头部不写尾部，`</body></html>` 永远缺失；
- `escapeHtml` 把所有 `<` `&` 转成实体，于是日志正文在编辑器里是 `&lt;` 噪声；
- 反过来，`.html` 不能被 `tail -f`、`grep`、`awk`、`less` 当日志用——这正是"日志里混进了 HTML"的直接来源。

**缺陷 C：页内 JS 与文件语义冲突（`ShineLog.cpp:208-318`）**

`rebuildLogView()` 在浏览器里移动 DOM、把日期卡片按倒序重排。而文件本身是追加写入的。视图与事实不一致，且每个文件只有一个日期，这段 JS 基本是死代码。

**缺陷 D：无去重、无限流**

10 条相同的线程池日志来自 `ThreadPool::init()`：`_stage++` 在创建线程的循环里被并发读写，10 条管家线程落到同一（或相近）step 上，各自调用一次 `setStageStatus`，于是各写一行。日志层没有任何"内容相同则折叠"或"每秒最多 N 条"的策略。

**缺陷 E：定位信息不足**

一条日志现有字段只有：时间（秒级）/ 级别（3 档）/ 模块 / 消息。缺少线程 id、源文件行号、阶段编号（`status` 命令也不显示编号）、序号、run id。最具体的一条：**日志里没有记录用户输入的命令本身**，只有"执行控制台命令 - 命令处理完成"，复盘时只能靠猜。

**缺陷 F：写盘路径低效**

`writeLine()` 每行 `open/close` 一次文件；`openLogFile()` 在持锁状态下做 `create_directories` 和最多 999 次 `exists()` 探测，会阻塞所有写日志的线程。

---

## 二、重构目标与原则

| 目标 | 衡量方式 |
|---|---|
| G1 日志是纯文本事实记录 | `.log` 可直接 `grep` / `tail` / `less`，正文零 HTML 标记 |
| G2 HTML 只作为派生产物 | 运行期不写 HTML；报告由命令按需生成 |
| G3 一次操作一组日志，可定位 | 任一条日志 3 步内定位到 模块:阶段 → 文件:行 → 线程 |
| G4 重复内容自动折叠 | 连续相同消息合并为 1 条 + 重复次数 |
| G5 级别可过滤 | TRACE/DEBUG/INFO/WARN/ERROR/FATAL，阈值可运行时调整 |
| G6 控制台只承载交互，不承载日志流 | 控制台零日志行，排障靠 `log tail / grep / open` |
| G7 自检信息不减少 | 每个阶段的执行次数、最近时间、最终状态都可查、可落盘 |

设计原则：

1. **自检调用点一个都不减少**。每个函数结尾仍然 `setStageStatus()`；改的只是"什么时候落盘、落成什么"。
2. **状态表与日志分工**。状态表回答"现在怎么样"，日志回答"什么时候变成这样"。
3. **热路径不做 I/O 之外的决策**。级别判定、去重判定在内存完成，写盘单锁 + 缓冲。
4. **保守兼容**。保留 `write / pass / error` 三个旧签名作为薄封装，避免一次性改动 30+ 调用点。

---

## 三、方案设计

### 3.1 总体分层

```
采集层   ShineLog::write(level, module, stage, msg, source_location)
   │
策略层   级别阈值 → 去重/折叠 → 限流（每秒 K 条/键）
   │
输出层   ├─ 纯文本文件 sink（.log，唯一事实源）
         ├─ 状态快照（SNAP 块：差分定时 + 退出全量 + 手动）
         └─ HTML 报告（按需，命令触发，非运行期）
   │
查询层   log 命令族：level / tail / grep / mark / open / report / stats / snapshot
```

**控制台不在输出层里**。日志一律不写 stdout/stderr，详见 3.4。

### 3.2 记录格式（纯文本，单一事实源）

```
19:34:40.123 | INFO  | Application:01 | #000007 | T-15432 | Application.cpp:85 | 打印启动提示 - 启动提示已打印
```

| 字段 | 说明 |
|---|---|
| 时间 | `HH:MM:SS.mmm`（日期在文件头与会话行，避免每行冗余） |
| 级别 | `TRACE/DEBUG/INFO/WARN/ERROR/FATAL`，另设伪级别 `CMD`（命令回显）、`MARK`（人工打点）、`SNAP`（快照分隔） |
| 模块:阶段 | `Net:03`，与 `status` 命令显示编号一致 |
| 序号 | 进程内单调递增，用于发现丢行与乱序 |
| 线程 | 线程 id 短写 |
| 位置 | `文件:行`，来自 `std::source_location`（项目已用 C++20 / GCC 13） |
| 消息 | 自由文本，最后一段，可用 ` - ` 分"摘要 - 细节" |

文件头（一次运行写一次，约 8 行）：

```
# InkingBackendFramework log
# run        r-20261021-193440-1234
# started    2026-10-21 19:34:40.123
# exe        D:/InkingBackendFramework/build/bin/InkingBackendFramework.exe
# cwd        D:/InkingBackendFramework/build/bin
# config     config/databaseConfig.json
# level      INFO
```

> 格式取舍：选竖线分隔而非 JSONL，因为主要读者是人和 `grep`；如后续要做工具消费，加 `--json` 开关输出第二份 JSONL，成本很低。

### 3.3 自检记录模型（回答"自检会不会失去意义"）

**自检不会减少，反而信息更全。** 现在的问题不是"自检写日志"，而是"同一条状态每轮重写一遍"——它既没有记录变化，也没有记录次数。

新模型把自检信息拆成两条互补的记录线：

| 触发条件 | 是否落盘 | 记录形态 | 回答什么问题 |
|---|---|---|---|
| 阶段首次出现 | ✅ INFO | `Net:03 \| 绑定端口 \| 已登记` | 本次运行到底走到了哪些阶段 |
| 状态或消息发生变化 | ✅ INFO / ERROR | `Net:03 \| 绑定端口 \| 失败 → 通过` | **什么时候变的、怎么变的**（这是自检的核心价值） |
| 失败（任何次数） | ✅ ERROR，**不折叠** | 附 `suggestion` | 故障证据，永不被吞掉 |
| 相同状态重复 | ❌ 仅内存计数 | `status` 显示 `通过 ×128，最近 19:41:02` | 执行频率、最近活跃时间 |
| 差分快照（默认 5min） | ✅ SNAP 块 | 只写变化项；稳态零字节 | 长跑进程的状态轨迹 |
| 退出快照 | ✅ SNAP 块 | 全模块阶段表 + 计数 | **最终状态可追溯**，即"运行结束时一切是什么样" |
| 心跳类阶段（Silent） | ❌ 仅计数 | 同"重复" | 证明"还活着"，但不刷屏 |

由此得到三条结论：

1. **"变化"永远落盘**：一次绑定失败、一次线程池退出、一次数据库连接失败，全都有独立记录，且带前后状态。
2. **"重复"转成计数**：不再丢弃信息，而是把 128 行同样的话压缩成 `×128 + 最近时间`，`status`、`log stats`、快照里都能看到。
3. **"最终态"有快照兜底**：即使某阶段一辈子没变化，退出时也会被完整写进日志。

`status` 命令的输出也随之增强（编号 + 计数 + 最近时间）：

```
[Net@ 0x...]
  [01] 申请本地的网络服务请求 - 通过 (×1, 19:34:40)
  [03] 绑定端口               - 通过 (×1, 19:34:40)
  [05] 启动！                 - 失败 (×3, 最近 19:41:02)  建议: 检查端口 9000 是否被占用
```

策略参数（`ShineBasicModule`）：

```cpp
enum class StageLogPolicy {
    OnChange,   // 默认：状态/消息变化、首次登记、任意失败 → 落盘；其余只计数
    Always,     // 关键阶段：每次都落盘（如每条 SQL 的审计）
    Silent      // 心跳/轮询：永不落盘，只计数（如"等待用户输入"）
};
void setStageStatus(int step, const std::string &name, bool statu,
                    const std::string &message,
                    StageLogPolicy policy = StageLogPolicy::OnChange);
```

### 3.4 控制台策略（日志不进控制台）

按你的意见调整：**日志只落文件，控制台保持干净**。

| 通道 | 内容 | 去向 |
|---|---|---|
| 控制台 | 启动横幅、密码提示、命令执行结果（`help` / `status` / `mysql-q` 的返回） | replxx，原有的 `UIMessageLibrary` 队列 |
| 日志 | 自检流水、异常、命令回显、快照 | `.log` 文件，**不写 stdout** |

需要日志时，三种取用方式：

```
log tail 50            最近 50 条（从内存环形缓冲读，不碰磁盘）
log grep ERROR         在缓冲中检索
log open               打印当前日志文件绝对路径，自行用编辑器打开
log report             生成 HTML 报告
```

保留一个**显式**排障开关（默认关闭），仅在需要盯着终端排障时临时启用：

```
INKING_LOG_CONSOLE=error    只把 ERROR 及以上镜像到控制台（仍走消息队列，不破坏提示符）
INKING_LOG_CONSOLE=off      默认值，零输出
```

这样既满足"存进文件、需要时调取"，也保留了无人值守场景下"一眼看到致命错误"的能力，且不会污染日常交互。

### 3.5 级别与默认阈值

- 编译期：Debug 默认 `DEBUG`，Release 默认 `INFO`；
- 运行期：环境变量 `INKING_LOG_LEVEL`，控制台 `log level <级别>`；
- 阈值只影响文件落盘，与 3.4 的控制台开关相互独立。

### 3.6 去重与限流

- 去重键：`(级别, 模块, 阶段, 消息摘要)`；
- **仅在连续相同时折叠**（中间插入任何其他日志即结算计数）；
- 结算输出：`… (重复 ×10，19:34:40.101 ~ 19:34:40.207)`；
- 限流：同一键每秒最多 5 条，超出部分只累计，结算时附 `suppressed=N`；
- `ERROR` / `FATAL` 永不被折叠抑制（可计数，但每条必须落盘）。

### 3.7 定位系统

| 能力 | 做法 |
|---|---|
| 命令级定位 | 每条控制台命令执行前写一行 `CMD`：`> mysql-q "select 1"`，作为时间线锚点 |
| 人工打点 | `log mark <文字>` 写一行 `MARK`，用于标记"我开始复现了" |
| 状态对齐 | `status` 输出带阶段编号与计数（见 3.3），与日志 `Net:03` 对齐 |
| 代码回溯 | 每条日志带 `文件:行`，编译期零成本（`source_location::current()`） |
| 实例定位 | run id 写入文件名与文件头，`LOG/latest.txt` 指针指向最近一次运行 |
| 快速检索 | `log grep <关键字>`、`log tail [n]`、`log stats` |

新增控制台命令（建议独立 `CommandLibraryTagLog`）：

```
log tail [n]                    从内存环形缓冲打印最近 n 条（默认 50）
log grep <关键字> [模块]        在内存缓冲中检索
log level [trace|debug|info|warn|error|fatal]   查看/调整当前进程阈值
log stats                       各模块/级别计数、折叠与限流统计
log snapshot                    立即把全模块阶段表写进日志
log mark <文字>                 打一个 MARK
log open                        打印当前日志文件绝对路径
log report                      从当前 .log 生成 HTML 报告
```

`CMD` / `MARK` / `SNAP` 不受阈值影响：即使把级别调到 `error`，"我执行了什么命令"这条锚点也会保留。

### 3.8 文件布局

```
LOG/
  2026-10-21/
    run-193440-1234.log       一次运行一个文件（run id 内嵌 pid）
  latest.txt                  文本指针：run=... / path=... / started=... / pid=...
```

- 不再写 `Log.html`；
- 跨零点：继续运行不换文件（一次运行 = 一个文件），只在文件内写一行日期变更标记。这比现在"跨零点劈成两份、还不留接续标记"更利于复盘一次会话；
- 旧目录 `LOG/2026-10-04/` 原样保留归档，不自动删除。

### 3.9 HTML 报告

`log report` 读取 `.log`，生成 `LOG/<日期>/report-<runid>.html`：

- 自包含单文件：日志原文内联进 `<script type="text/plain">`，JS 本地解析，双击即开，无 CORS 依赖；
- 沿用现有暗色主题（保留视觉资产，去掉它作为"日志本体"的地位）；
- 功能：级别筛选、模块筛选、关键字搜索、只看错误、重复条目折叠展开、ERROR 一键跳转、快照折叠面板。

### 3.10 接口变更（示意）

```cpp
enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Fatal };

class ShineLog {
public:
    static void init(const LogOptions &options);   // Application 启动时调用一次
    static void setLevel(LogLevel level);
    static void shutdown();                        // 退出快照 + flush + 写文件尾

    static void write(LogLevel level, std::string_view module, int stage,
                      std::string_view message,
                      const std::source_location &site = std::source_location::current());

    static void snapshot();                        // 由 ShineStatusChecker 汇总阶段表

    // 兼容层：旧调用点零改动即可编译
    static void write(const std::string &module, const std::string &message);
    static void pass(const std::string &module, const std::string &message);
    static void error(const std::string &module, const std::string &message);
};
```

---

## 四、代码改动清单

| 文件 | 改动 |
|---|---|
| `basic/ShineLog.h` | 新增 `LogLevel` / `LogOptions` / `init` / `shutdown` / `setLevel` / `snapshot` / 内存环形缓冲；保留 3 个旧签名 |
| `basic/ShineLog.cpp` | 重写：纯文本 sink、缓冲写盘、去重折叠、限流、单次目录创建、文件头尾、线程 id、序号、SNAP 块；**不加控制台输出** |
| `basic/ShineBasicModule.cpp/.h` | 修复缺陷 A（首建阶段赋值 + 空消息兜底）；引入 `StageLogPolicy`；增加「执行次数 / 首次时间 / 最近时间」计数 |
| `basic/ShineStatusChecker.*` | 提供遍历全部模块与阶段的接口，供 `snapshot()` 与 `status` 使用 |
| `src/thread/ThreadPool.cpp` | 修复 `_stage++` 竞态：阶段号用线程索引 `i` 捕获；10 条管家日志聚合为 1 条 INFO + 10 条 TRACE |
| `src/thread/TaskQueueLoop.cpp` | 异常日志带上任务标识（可读文本，不含敏感内容） |
| `src/ui/UIThread.cpp` | "读取输入/执行命令"改为 `Silent`（只计数）；命令原文写 `CMD` 行（截断 200 字符）；输出失败保持 ERROR |
| `src/network/Net.cpp` | 5 个启动阶段聚合为 1 条结果日志（失败时逐项展开）；连接收发类日志归 DEBUG |
| `src/database/DatabaseManager.cpp` | 每条 SQL 用 `Always`（审计价值），补执行耗时；超阈值升 WARN |
| `src/core/Application.cpp` | `ShineLog::init()` / `shutdown()` 落位；启动横幅带 run id |
| `src/command/CommandLibrary/*` | 新增 `log` 命令族；`status` 输出补编号、计数、最近时间 |
| `CMakeLists.txt` | 如新增源文件则登记 |
| `README.md` / `docs/` | 更新日志章节（路径、格式、命令、级别、控制台策略） |

---

## 五、分阶段实施

| 阶段 | 内容 | 产出 |
|---|---|---|
| **P0** | `ShineLog` 内部重写：纯文本格式、级别、缓冲、去重、限流、文件头尾、线程/序号/位置、SNAP 块 | 新 `.log` 产物，旧 API 仍可用 |
| **P1** | `ShineBasicModule` 解耦：修缺陷 A、加 `StageLogPolicy` 与计数器、`OnChange` 默认行为 | 空日志消失、心跳只计数 |
| **P2** | 调用点清理：ThreadPool 竞态、UIThread 静默 + 命令回显、Net 聚合、DB 审计 | 启动+命令日志量降到 10 条以内 |
| **P3** | `log` 命令族 + HTML 报告生成器 + 定时/退出快照 | 调取与定位闭环 |
| **P4** | 构建、回归、文档更新、旧日志归档说明 | 验收报告 |

每阶段完成后单独给出可验证结果，不一次性合入。

---

## 六、验收标准

| 项 | 标准 |
|---|---|
| 体积 | 一次"启动 → status → exit"的运行：流水 + 退出快照合计 ≤ 2 KB（改造前 8756 字节；未计入快照的流水部分约 0.8 KB） |
| 噪声 | 该场景日志 ≤ 10 条，无空消息条目，无重复条目 |
| 纯净度 | `.log` 中 HTML 标记数 = 0，`grep` / `tail` 可直接使用 |
| 折叠 | 10 线程初始化只产生 1 条聚合记录；重复条目以 `×N` 计数出现 |
| 自检完整性 | `status` 对每个阶段显示「状态 + 执行次数 + 最近时间」；退出后日志末尾有 SNAP 块，含全部已登记阶段 |
| 控制台 | 除启动横幅与命令结果外，零日志行 |
| 定位 | 任取一条 ERROR，能直接得到 模块:阶段、文件:行、线程、run id |
| 可用性 | `log report` 生成的 HTML 双击可开，支持级别筛选与错误跳转 |
| 稳定性 | 多线程并发写不漏行、不乱序（序号连续） |

---

## 七、风险与取舍

| 风险 | 说明 | 应对 |
|---|---|---|
| 语义变化 | `OnChange` 默认策略会让"每轮都有记录"的现状消失 | 变化与失败必记 + 计数可查 + 定时/退出快照三重兜底；DB 审计用 `Always` |
| 无控制台输出 | 有人值守排障时看不到实时错误 | `log tail` / `log grep` + 可选 `INKING_LOG_CONSOLE=error` 显式开关 |
| 热路径锁竞争 | 10 条管家线程高频写日志 | 内存缓冲 + 批量 flush；`openLogFile` 移出锁外，仅 init 时执行一次 |
| 快照开销 | 阶段数量多时序列化成本 | 差分快照：状态不变时零字节；间隔 5 分钟，可环境变量覆盖 |
| 旧日志清理 | `LOG/2026-10-04/` 18 个文件是历史证据 | 不删，仅归档；如需清理会单独征求同意 |
| 兼容性 | 30+ 调用点 | 旧 3 个签名保留为薄封装，P2 再逐个收敛 |

---

## 八、决策点（已确认）

| 编号 | 决策 | 结果 |
|---|---|---|
| D1 | 日志格式 | **竖线分隔纯文本**，人读为主，需要时再加 `--json` |
| D2 | 文件粒度 | **一次运行一个 `.log` + `latest.txt`**，一次会话一个文件，复盘最直接 |
| D3 | HTML 去留 | **仅命令按需生成报告**，保留视觉资产，但不再污染热路径 |
| D4 | 控制台 | **零日志输出** + 可选 `INKING_LOG_CONSOLE` 开关 |
| D5 | 快照节奏 | **差分 5min + 退出 + 手动**；稳态零写入 |

---

## 九、实施结果（2026-10-21）

| 指标 | 改造前 | 改造后 |
|---|---|---|
| 单次运行文件体积（启动 → status → exit） | 8756 字节 | 1963 字节（其中退出快照 6 行） |
| 有效流水条数 | 7 条（1 条空、2 对重复） | 5 条（1 应用 + 1 启动 + 1 停止 + 2 条 CMD），零空行、零重复 |
| HTML 标记 | 占文件 84%，285 行模板 | 0 |
| 是否记录用户输入的命令 | 否 | 是（`CMD` 行） |
| 线程/文件行/序号/run id | 无 | 全部具备 |
| 重复抑制 | 无 | 连续折叠 + 每秒 5 条限流 + 计数器 |
| 心跳日志 | 每轮 2 行 | 0 行（`Silent`，`status` 里 `×N` 可见） |
| 快照 | 无 | 差分 5min（稳态零字节）+ 退出全量 + `log snapshot` |

验证方式：

- `build/log_check`（临时桩程序，验证後已删除）验证折叠（`×5`）、限流（3 条被抑制并计入页脚）、
  `ERROR` 不折叠、多行消息压成单行；
- 实跑 3 秒、快照间隔 1 秒：周期快照只在首轮写一次完整块，其后 2 秒零写入；退出时写全量；
- `log report` 生成的报告用 node 校验内嵌脚本语法通过，字段解析 9/9 正确（消息内的 ` | ` 不会被误切）。

一处与方案不同的实现细节：`Silent` 策略只抑制"成功流水"，**失败仍然写 `ERROR`**。
心跳静默的前提是它一直正常；一旦心跳失败，那正是最需要留证的时刻。

回归风险：`Application` 在 `run()` 结束时显式 `shutdown()`，`atexit` 兜底；
日志状态是永不析构的堆对象，静态析构期的日志调用不会 use-after-free。
