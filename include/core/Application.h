#ifndef INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H
#define INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H

#include "basic/ShineBasicModule.h"
#include "ui/UIThread.h"

#include <atomic>
#include <memory>
#include <thread>

class Net;

inline constexpr const char* kApplicationModuleName = "Application";

/**
 * @brief 一次启动的选项，来源是命令行（见 core/CommandLine.h）。
 *
 * 拆成结构体而不是给 run() 加一串 bool：这些开关之间有关联
 * （serviceMode 影响是否校验密码、是否安装信号处理、控制台读完输入后是否退出），
 * 用命名参数能把关联写进注释里，用位置参数只能靠记忆。
 */
struct ApplicationOptions
{
    /** 常驻模式：安装终止信号处理，标准输入关闭后进程继续运行 */
    bool serviceMode = false;
    /** 是否拉起网络服务 */
    bool startNetwork = false;
};

/**Application是后端程序的入口 */
class Application : public ShineBasicModule
{
public:
    Application();
    /** 启动后端并阻塞到进程该结束为止；返回值即进程退出码（0 正常，1 密码校验失败） */
    int run(const ApplicationOptions &options = ApplicationOptions{});
    ~Application() override;

    std::string moduleName() const override
    {
        return kApplicationModuleName;
    };

private:
    /**
     * @brief 启动前的基础提示阶段。
     */
    void showStartupMessage(bool serviceMode);

    /** 把网络服务交给任务队列的后台线程执行；重复调用无副作用 */
    void startNetworkService();

    /** 通知网络线程退出并等待它结束；返回是否在超时前结束 */
    bool stopNetworkService();

    UIThread _uiThread;
    std::thread _uiWorker;
    /**
     * 网络服务用 shared_ptr 持有，并且把同一个 shared_ptr 传进后台任务。
     * 用裸指针或成员捕获都会在退出路径上踩到"Application 已析构、网络线程还在跑"，
     * 而网络线程负责监听 socket，悬空引用直接表现为退出时偶发崩溃。
     */
    std::shared_ptr<Net> _network;
    std::atomic<bool> _networkStarted{false};
    /** 同上：完成标志也必须是网络线程与主线程共享的所有权，不能挂在对象上 */
    std::shared_ptr<std::atomic<bool>> _networkFinished = std::make_shared<std::atomic<bool>>(true);
};

#endif // INKING_BACKEND_FRAMEWORK_CORE_APPLICATION_H
