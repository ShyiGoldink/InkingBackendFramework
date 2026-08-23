#ifndef INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H
#define INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H

#include "basic/ShineBasicModule.h"
#include "ui/UIThread.h"

#include <thread>

inline constexpr const char* kApplicationModuleName = "Application";

/**Application是后端程序的入口 */
class Application : public ShineBasicModule
{
public:
    Application();
    // 调用run()方法启动后端程序
    void run();
    ~Application() override;

    std::string moduleName() const override{
        return kApplicationModuleName;
    };

private:
    /**
     * @brief 启动前的基础提示阶段。
     */
    void showStartupMessage();

    UIThread _uiThread;
    std::thread _uiWorker;
};

#endif // INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H
