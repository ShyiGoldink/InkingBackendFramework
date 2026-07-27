#ifndef INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H
#define INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H

#include "basic/ShineBasicModule.h"
#include "ui/UiThread.h"

/**Application是后端程序的入口 */
class Application : public ShineBasicModule
{
public:
    Application();
    // 调用run()方法启动后端程序
    void run();
    ~Application() override;

    std::string moduleName() const override;

private:
    /**
     * @brief 启动前的基础提示阶段。
     */
    void showStartupMessage();

    UiThread _uiThread;
};

#endif // INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H
