#include "thread/ThreadPool.h"
#include "thread/TaskQueueLoop.h"
#include "basic/ShineLog.h"

#include <string>

TaskQueueLoop::TaskQueueLoop()
{
    _threadLoop = std::make_unique<ThreadPool>();
    _threadLoop->init(
        // 谓词：队列非空时唤醒管家线程
        [this]() { return !isEmpty(); },
        // 执行体：每次从队列中取出一个任务执行
        [this]() { executeTask(); });
}

TaskQueueLoop::~TaskQueueLoop()
{
    if (!_threadLoop)
    {
        return;
    }
    // 先停止并 join 全部管家线程，再销毁成员，
    // 避免管家线程在回调中访问已析构的 this
    _threadLoop->quit();
    _threadLoop.reset();
}

void TaskQueueLoop::addTask(Task<std::any> task)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(std::make_unique<Task<std::any>>(std::move(task)));
    }
    // 唤醒管家线程处理新任务
    _threadLoop->wake();
}

bool TaskQueueLoop::isEmpty() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _queue.empty();
}

void TaskQueueLoop::executeTask()
{
    std::unique_ptr<Task<std::any>> task;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty())
        {
            return;
        }
        task = std::move(_queue.front());
        _queue.pop();
    }

    // 在锁外执行任务：任务内部若再次 addTask()，不会造成死锁。
    // 任务作为独立入口执行，入参使用空的 any（无上游值）。
    // 防御性兜底：action 抛出的异常绝不能让管家线程逃逸（会导致进程 terminate），
    // 统一在这里捕获并记入日志，单个任务失败不影响线程池继续工作。
    try
    {
        task->execute(std::any{});
    }
    catch (const std::exception &exception)
    {
        ShineLog::error("TaskQueueLoop", std::string("任务执行抛出异常：") + exception.what());
    }
    catch (...)
    {
        ShineLog::error("TaskQueueLoop", "任务执行抛出未知异常");
    }
}
