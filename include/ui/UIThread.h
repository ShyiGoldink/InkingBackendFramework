#ifndef INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H
#define INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H

#include "basic/ShineBasicModule.h"
#include "command/CommandCenter.h"

#include <mutex>
#include <string>

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
    void renderQueuedMessages();

    std::string _saveInput;       /**用户输入储存 */
    std::string _liveInput;       /**当前用户编辑中的实时输入 */
    int _liveCursorPosition = 0; /**当前输入光标位置 */
    std::mutex _consoleMutex;     /**控制台输出互斥量 */
    CommandCenter _commandCenter; /**指令中心 */
    bool _running = false;
};

#endif // INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H
