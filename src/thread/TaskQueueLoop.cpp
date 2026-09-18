#include "thread/ThreadPool.h"
#include "thread/TaskQueueLoop.h"
#include "basic/ShineLog.h"

#include <string>

namespace
{
/** 队列为空时管家线程最多睡多久（有任务入队时会立刻被唤醒，所以这个值只是兜底） */
constexpr auto kIdleWait = std::chrono::hours(1);
}

TaskQueueLoop::TaskQueueLoop()
{
    _threadLoop = std::make_unique<ThreadPool>();
    _threadLoop->init(
        // 谓词：有任务到期时才唤醒管家线程（只看队列非空不行，延迟任务会空转）
        [this]() { return hasDueTask(); },
        // 执行体：每次从队列中取出一个任务执行
        [this]() { executeTask(); },
        // 等待提示：睡到最近一个任务到期为止
        [this]() { return timeUntilNextDue(); });
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
    addTask(std::move(task), std::chrono::milliseconds(0));
}

void TaskQueueLoop::addTask(Task<std::any> task, std::chrono::milliseconds delay)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.emplace(Clock::now() + delay, std::make_unique<Task<std::any>>(std::move(task)));
    }
    // 只叫醒一个管家线程：一条任务只需要一个执行者。
    // 真有 N 条任务同时入队时，是 N 次入队各叫醒一个人，不会因为"只叫一个"而积压；
    // 如果需要更多人手（比如在跑的管家线程已经很多），管家线程干完手里的活
    // 回到循环顶部会重新判断谓词，自己把队列里的下一件活取走。
    _threadLoop->wake();
}

bool TaskQueueLoop::isEmpty() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _queue.empty();
}

bool TaskQueueLoop::hasDueTask() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    // multimap 按到期时刻排序，所以只看第一个就够了
    return !_queue.empty() && _queue.begin()->first <= Clock::now();
}

std::chrono::milliseconds TaskQueueLoop::timeUntilNextDue() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_queue.empty())
    {
        return kIdleWait;
    }

    const auto now = Clock::now();
    const auto due = _queue.begin()->first;
    if (due <= now)
    {
        return std::chrono::milliseconds(0);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(due - now);
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
        auto earliest = _queue.begin();
        if (earliest->first > Clock::now())
        {
            return; // 还没到期的任务不能提前跑（谓词已经挡了一层，这里兜底）
        }
        task = std::move(earliest->second);
        _queue.erase(earliest);
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
