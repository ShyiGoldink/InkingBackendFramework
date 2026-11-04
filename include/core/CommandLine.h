#ifndef INKING_BACKEND_FRAMEWORK_CORE_COMMAND_LINE_H
#define INKING_BACKEND_FRAMEWORK_CORE_COMMAND_LINE_H

#include <ostream>
#include <string>

/**
 * @brief 一次启动的行为模式。
 *
 * 只有两种真正会跑起来的模式，另外三个是"说完就走"的查询模式。
 * 区分 Service 和 Interactive 的唯一标准是**谁来决定进程什么时候结束**：
 * Interactive 由控制台决定，Service 由终止信号决定。
 */
enum class StartupMode
{
    Interactive, /**< 默认（无参数）：校验密码、进入控制台，控制台关闭即结束进程 */
    Service,     /**< --start：常驻启动，标准输入关闭不结束进程，收到终止信号才退出 */
    Help,        /**< --help：打印用法后退出 */
    Version,     /**< --version：打印版本后退出 */
    Invalid      /**< 参数无法识别，由 main 打印错误并以参数错误码退出 */
};

/** 命令行解析结果。 */
struct CommandLineOptions
{
    StartupMode mode = StartupMode::Interactive;
    /** 是否随本次启动拉起网络服务；--start 固定为 true，无参数时沿用 INKING_AUTO_NETWORK */
    bool startNetwork = false;
    /** mode 为 Invalid 时的错误说明，直接展示给用户 */
    std::string error;
};

/**
 * @brief 命令行入口。
 *
 * 这里的输出（帮助、版本、错误提示）都走流参数而不是 std::cout，
 * 是因为"问用法"要写 stdout，而"参数写错了"要写 stderr：
 * 脚本里 `--help > /dev/null` 和 `--bad 2> /dev/null` 的语义不一样。
 */
class CommandLine
{
public:
    /**
     * @brief 解析 argv。
     *
     * 只认显式参数，不做前缀模糊匹配：`--sta` 会被判为参数错误，
     * 而不是"猜你想要 --start"。启动模式写错却照样跑起来，比直接报错更难查。
     */
    static CommandLineOptions parse(int argc, char **argv);
    /** 打印用法 */
    static void printUsage(std::ostream &out);
    /** 打印版本 */
    static void printVersion(std::ostream &out);

    /** 程序名，帮助与版本输出共用 */
    static constexpr const char *kProgramName = "InkingBackendFramework";
    /** 版本号：命令行 `--version` 与启动横幅共用同一份 */
    static constexpr const char *kVersion = "0.1.0";
};

#endif // INKING_BACKEND_FRAMEWORK_CORE_COMMAND_LINE_H
