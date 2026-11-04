#ifndef INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H
#define INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H

#include "basic/ShineBasicModule.h"
#include "command/CommandCenter.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace replxx
{
    class Replxx;
}

inline constexpr const char *kUIThreadModuleName = "UIThread";

/**
 * @brief 控制台交互模块。
 *
 * 设计为：一个线程专门读入用户输入，另一个线程专门负责输出消息。
 * 通过共享的输入缓存与 console 锁来实现“看起来像没有被阻塞”的交互体验。
 *
 * 两种输入方式取决于标准输入是不是终端：
 *   - 终端：replxx（历史、方向键、整行重绘）；
 *   - 管道/重定向/`/dev/null`：逐行读取。replxx 要改终端属性，在非终端上不可用。
 */
class UIThread : public ShineBasicModule
{
public:
    UIThread();
    ~UIThread() override;

    std::string moduleName() const override
    {
        return kUIThreadModuleName;
    };

    /**
     * @brief 启动交互循环。
     *
     * @param stayAliveWhenInputClosed 标准输入关闭后是否让进程继续运行。
     *        false（默认）：保持原有语义——输入结束即结束进程；
     *        true（常驻模式）：转成等待终止信号，进程不因"没人再输入了"而退出。
     */
    void run(bool stayAliveWhenInputClosed = false);

private:
    void registerStages();
    /**
     * @brief 把消息队列里的内容交给 writeOut 输出。
     *
     * 之所以要经过 replxx 而不是直接打印：消息可能刚好在用户编辑时到达，
     * 只有 replxx 知道自己把提示符和输入画在了哪一行，由它来「清行、打印消息、重画」
     * 才不会把用户正在敲的那一行弄乱。非 replxx 模式下 writeOut 直接写标准输出。
     */
    void renderQueuedMessages(const std::function<void(const std::string &)> &writeOut);
    /** 从管道/重定向读取一行；返回 false 表示 EOF、读失败或收到终止信号 */
    bool readLineFromPipe(std::string &line);
    /** 标准输入关闭后的常驻等待：只等终止信号，不结束进程 */
    void waitAsResident();

    std::string _saveInput;       /**用户输入储存 */
    CommandCenter _commandCenter; /**指令中心 */
    std::atomic<bool> _running{false};
    /**
     * 当前是否由 replxx 负责输出。
     *
     * replxx 的 write() 只是把内容排进它自己的队列，真正的绘制发生在 input() 那一轮循环里。
     * 常驻模式下主线程不再调用 input()，继续往 replxx 里写就是"写进黑洞"，
     * 所以进入常驻时把这个开关关掉，输出改走标准输出。
     */
    std::atomic<bool> _replxxActive{false};
    /** 保护非 replxx 模式下的标准输出：输出线程与提示符不能互相插队 */
    std::mutex _outputMutex;
};

#endif // INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H
