#ifndef INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H
#define INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H

#include "basic/ShineBasicModule.h"
#include "command/CommandCenter.h"

#include <atomic>
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
     */
    void run();

private:
    void registerStages();
    /**
     * @brief 把消息队列里的内容交给 replxx 输出。
     *
     * 之所以要经过 replxx 而不是直接打印：消息可能刚好在用户编辑时到达，
     * 只有 replxx 知道自己把提示符和输入画在了哪一行，由它来「清行、打印消息、重画」
     * 才不会把用户正在敲的那一行弄乱。
     */
    void renderQueuedMessages(replxx::Replxx &rx);

    std::string _saveInput;       /**用户输入储存 */
    CommandCenter _commandCenter; /**指令中心 */
    std::atomic<bool> _running{false};
};

#endif // INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H
