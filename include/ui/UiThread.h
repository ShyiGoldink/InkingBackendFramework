#ifndef INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H
#define INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H

#include "basic/ShineBasicModule.h"
#include "command/CommandCenter.h"

inline constexpr const char* kUIThreadModuleName = "UIThread";

/**
 * @brief 控制台交互模块。
 *
 * 当前在主线程中运行交互循环，但保留 UiThread 命名，
 * 方便后续真正拆分线程或接入事件循环。
 */
class UIThread : public ShineBasicModule
{
public:
    UIThread();
    ~UIThread() override;

    std::string moduleName() const override{
        return kUIThreadModuleName;
    };

    /**
     * @brief 启动控制台交互循环。
     */
    void run();

private:
    void registerStages();
    void printPrompt() const;

    CommandCenter _commandCenter;
    bool _running = false;
};

#endif // INKING_BACKEND_FRAMEWORK_UI_UI_THREAD_H
