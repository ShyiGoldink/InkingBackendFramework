#ifndef INKING_BACKEND_FRAMEWORK_THREAD_POOL_H
#define INKING_BACKEND_FRAMEWORK_THREAD_POOL_H

#include <atomic>
#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "basic/ShineBasicModule.h"

inline constexpr const size_t THREADNUM = 10;


/*
仔细思考了一下，我注意到了一个不太好的问题
线程池的使用场景不太方便通过RAII的思想来管理
主要是，它用完归还的时机不太好配置
令牌模式下：
通过令牌的生命周期来管理借出的线程，令牌生命周期结束时归还，借用方无需手动归还。
但本类只是一个局限线程池，所以这个生命周期交给任务队列去管理吧
*/

/**
 * 这是一个线程池，用于实现多线程以及配套的内容
 * 主要是通过多线程的方式实现异步逻辑
 */
class ThreadPool:public ShineBasicModule
{
public:
ThreadPool();
~ThreadPool();
/**
 * @brief 受限线程池初始化函数：注意传入的内容和生命周期！
 * @param predicate 必须是返回bool值的函数，作为谓词，代表“唤醒线程的条件”
 * @param execute 必须是返回void值的函数，作为执行逻辑，代表“唤醒线程之后做什么”
 */
void init(std::function<bool()> predicate,std::function<void()> execute);
/**线程池关闭函数 */
void quit();
/**唤醒正在等待的管家线程，例如新任务入队后调用 */
void wake();

std::string moduleName()const override{
    return "ThreadPool";
}

private:
std::array<std::unique_ptr<std::thread>,THREADNUM> _threads;/**过多的线程非常容易影响效率，这个和连接不一样，所以我这边建议是使用array将线程的数量钉死 */
std::mutex _mutex;/**通过锁来保护多线程进程 */
std::condition_variable _cv;/**条件变量，整个线程池使用同一个环境变量，通过公平唤醒来提升线程池的性能*/
std::function<bool()> _predicateCallback;/**谓词回调函数，初始化时用于判断环境变量 */
std::function<void()> _executeCallback;/**执行回调函数，满足条件变量后执行 */
std::atomic<bool> _isQuit{false};/**线程池退出标志位 */
int _stage;/*一个小辅助数，用于给次注册管家函数添加阶段差别*/
/**安全关闭全部线程 */
void release();
/**管理函数，线程出生起就要执行这个函数，这个函数负责管理线程 */
void butler();

};

#endif //INKING_BACKEND_FRAMEWORK_THREAD_POOL_H
