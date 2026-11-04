#ifndef INKING_BACKEND_FRAMEWORK_CORE_CONSOLE_ENVIRONMENT_H
#define INKING_BACKEND_FRAMEWORK_CORE_CONSOLE_ENVIRONMENT_H

/**
 * @brief 当前进程的控制台环境。
 *
 * 它回答的不是"做一件事"，而是"这个进程现在被接到了什么上面"。有三个地方依赖它，
 * 而且判断错的后果都不是报错，而是行为不对：
 *   - Application 决定要不要校验控制台密码（管道/systemd 下没人能输密码）；
 *   - UIThread 决定用 replxx 还是逐行读取（replxx 要改终端属性，非终端下不可用）；
 *   - UIThread 决定要不要往输出里打提示符（脚本消费输出时提示符是噪声）。
 *
 * 命名说明：
 *   - 不叫 *Tool：Tool 在本项目里指 `PasswordTool` / `JsonTool` 这类"做一件具体小事"的工具；
 *   - 不叫 *Manager：Manager 留给持有状态、需要取实例的单例；
 *   - 这里只是一组无状态的环境查询，所以叫 Environment，并且只有静态方法、不能实例化。
 */
class ConsoleEnvironment
{
public:
    /** 纯静态查询，不希望任何人创建它：创建对象既没有意义，也暗示了不存在的状态 */
    ConsoleEnvironment() = delete;

    /** 标准输入是否接在交互终端上：管道、/dev/null、systemd、容器里都是 false */
    static bool stdinIsTerminal();

    /** 标准输出是否接在交互终端上 */
    static bool stdoutIsTerminal();
};

#endif // INKING_BACKEND_FRAMEWORK_CORE_CONSOLE_ENVIRONMENT_H
