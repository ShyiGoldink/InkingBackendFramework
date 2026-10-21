#ifndef INKING_BACKEND_FRAMEWORK_BASIC_SHINE_LOG_H
#define INKING_BACKEND_FRAMEWORK_BASIC_SHINE_LOG_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief 日志级别。
 *
 * 只有"能按级别过滤"，才敢在热路径上留日志：心跳降到 Trace/Silent，
 * 故障留在 Error，默认阈值下互不干扰。
 */
enum class LogLevel : std::uint8_t
{
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

/** 默认阈值：Debug 构建记 Debug，Release 构建记 Info。 */
#ifdef NDEBUG
inline constexpr LogLevel kDefaultLogLevel = LogLevel::Info;
#else
inline constexpr LogLevel kDefaultLogLevel = LogLevel::Debug;
#endif

/**
 * @brief 日志初始化参数。
 */
struct LogOptions
{
    /** 落盘阈值，低于该级别的记录直接丢弃。 */
    LogLevel level = kDefaultLogLevel;
    /** 是否启用差分快照（状态变化才写，稳态零字节）。 */
    bool snapshotEnabled = true;
    /** 快照节奏。 */
    std::chrono::seconds snapshotInterval{300};
};

/**
 * @brief 纯文本日志工具。
 *
 * 设计要点（与旧版 HTML 日志的区别）：
 *
 * 1. **纯文本**。文件是可被 grep/tail/less 直接消费的 `.log`，运行期不再写任何 HTML。
 *    需要浏览器视图时用 `log report` 从 `.log` 生成报告，HTML 只作为派生产物。
 *
 * 2. **按级别过滤 + 折叠**。连续相同的消息只落一条，后续合并成一行 `FOLD`
 *    并带上重复次数；同一键每秒超过 5 条则只计数不落盘。
 *
 * 3. **每行自带定位**：级别、模块:阶段、序号、线程号、源文件:行。
 *    拿到任意一行就能回到代码、回到线程、和 `status` 命令里的阶段编号对上。
 *
 * 4. **一次运行一个文件**：`LOG/<日期>/run-<时-分-秒>-<pid>.log`，
 *    外加 `LOG/latest.txt` 指向最近一次运行，跨零点不换文件（一次会话一个文件才好复盘）。
 *
 * 5. **不写控制台**。控制台只承载交互；排障用 `log tail / grep / open`。
 *    无人值守场景可显式打开镜像：环境变量 `INKING_LOG_CONSOLE=error|warn|info`。
 */
class ShineLog
{
public:
    /** 控制台镜像回调：仅在显式打开 `INKING_LOG_CONSOLE` 时被调用。 */
    using MirrorSink = std::function<void(LogLevel, const std::string &)>;

    /**
     * @brief 初始化日志系统：创建文件、写文件头、启动快照线程。
     *
     * 可重复调用，只有第一次生效。级别与镜像开关会读取环境变量：
     * `INKING_LOG_LEVEL`、`INKING_LOG_CONSOLE`、`INKING_LOG_SNAPSHOT`。
     */
    static void init(const LogOptions &options = LogOptions{});

    /**
     * @brief 收尾：写退出快照、文件尾、刷新并关闭文件。
     *
     * 可重复调用。未调用时由 atexit 兜底（进程正常退出即会执行）。
     */
    static void shutdown();

    /** 安装控制台镜像出口；是否真的镜像由 `INKING_LOG_CONSOLE` 决定。 */
    static void setMirrorSink(MirrorSink sink);

    /** 调整运行时阈值（`log level` 命令用）。 */
    static void setLevel(LogLevel level);
    /** 读取当前阈值。 */
    static LogLevel level();
    /** 解析级别名字；无法识别时返回 false。 */
    static bool parseLevel(std::string_view text, LogLevel &result);

    /**
     * @brief 写一条日志。
     * @param level 级别。
     * @param module 模块名。
     * @param stage 阶段编号；没有阶段概念时传 -1。
     * @param message 日志内容。
     * @param site 自动捕获的调用位置（文件:行），正常情况下不要手填。
     */
    static void log(LogLevel level, std::string_view module, int stage, std::string_view message,
                    const std::source_location &site = std::source_location::current());

    /** 写一行命令回显（伪级别 CMD），作为"这一次操作"的时间线锚点。 */
    static void command(std::string_view line,
                        const std::source_location &site = std::source_location::current());

    /** 写一行人工打点（伪级别 MARK），用于标记复现起点。 */
    static void mark(std::string_view text,
                     const std::source_location &site = std::source_location::current());

    /**
     * @brief 写一份状态快照。
     *
     * @param reason 触发原因：`manual` / `exit` 写全量；`periodic` 只写变化项，
     *               没有任何变化时一个字节都不写。
     */
    static void snapshot(std::string_view reason);

    /** 当前日志文件绝对路径；尚未初始化时返回空串。 */
    static std::string filePath();

    /** 读取内存环形缓冲里最近 count 行。 */
    static std::vector<std::string> tail(std::size_t count);

    /** 在内存环形缓冲里按子串检索；module 为空表示不过滤模块。 */
    static std::vector<std::string> grep(std::string_view pattern, std::string_view module);

    /** 统计摘要：总条数、折叠数、限流数、按级别与模块的计数。 */
    static std::string statistics();

    /** 当前时间，形如 19:34:40，供自检阶段记录首次/最近时间。 */
    static std::string timestamp();

    // ---- 兼容层：旧调用点无需改动 ----

    /** 等价于 Info。 */
    static void write(const std::string &moduleName, const std::string &message);
    /** 等价于 Info，语义上表示"通过"。 */
    static void pass(const std::string &moduleName, const std::string &message);
    /** 等价于 Error。 */
    static void error(const std::string &moduleName, const std::string &message);
};

#endif // INKING_BACKEND_FRAMEWORK_BASIC_SHINE_LOG_H
