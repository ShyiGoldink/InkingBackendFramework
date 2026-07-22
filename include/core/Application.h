#ifndef INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H
#define INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H

#include "Basic/ShineBasicModule.h"

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
     * @brief 初始化前的问候阶段。
     *
     * 成功输出问候语后，会向日志写入“玘小姐已上线”。
     */
    void sayHello();
};

#endif // INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H
