#ifndef INKING_BACKEND_FRAMEWORK_TASK_QUEUE_LOOP_H
#define INKING_BACKEND_FRAMEWORK_TASK_QUEUE_LOOP_H

#include <any>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>

#include "dataStruct/TaskStruct.h"

class ThreadPool;

/**
 * @brief 基于 ThreadPool 的任务队列循环。
 *
 * 构造时自动创建并启动线程池：队列为空时管家线程休眠，
 * addTask() 入队后唤醒它们，每个管家线程一次取一个任务执行。
 * 析构时先停止并 join 全部管家线程，保证线程不会在成员销毁后访问本对象。
 */
class TaskQueueLoop
{
public:
    /**返回懒加载的线程安全单例 */
    static TaskQueueLoop &instance()
    {
        static TaskQueueLoop inst;
        return inst;
    }

    TaskQueueLoop(const TaskQueueLoop &) = delete;
    TaskQueueLoop &operator=(const TaskQueueLoop &) = delete;
    TaskQueueLoop(TaskQueueLoop &&) = delete;
    TaskQueueLoop &operator=(TaskQueueLoop &&) = delete;

    /**添加任务（线程安全），任务会整体（含依赖/后继）在某个管家线程中执行 */
    void addTask(Task<std::any> task);
    /**
     * 延迟添加任务（线程安全）：delay 之后才会被管家线程取走执行。
     * 这是本队列的定时器能力——任务到点时没有别人会来通知，
     * 靠的是线程池那边的等待超时（见 ThreadPool::init 的 waitHint）。
     */
    void addTask(Task<std::any> task, std::chrono::milliseconds delay);
    /**返回任务队列是否为空（线程安全，含还没到期的延迟任务） */
    bool isEmpty() const;
    /**队列里是否有已经到期的任务（线程安全），这才是真正的唤醒条件 */
    bool hasDueTask() const;
    /**距离最近一个任务到期还有多久（线程安全），队列为空时返回一个较大的值 */
    std::chrono::milliseconds timeUntilNextDue() const;
    /**执行任务：作为线程池的执行函数，从任务队列中取出一个任务并执行 */
    void executeTask();

private:
    TaskQueueLoop();
    ~TaskQueueLoop();

    using Clock = std::chrono::steady_clock;

    std::unique_ptr<ThreadPool> _threadLoop;/**线程池 */
    /**
     * 任务队列：按"到期时刻"排序。
     * 用 multimap 而不是 queue，是因为延迟任务需要一个按时间有序的结构；
     * 到期时刻相同的任务，multimap 保证按插入顺序排列，所以立即任务之间仍是先进先出。
     */
    std::multimap<Clock::time_point, std::unique_ptr<Task<std::any>>> _queue;
    mutable std::mutex _mutex;/**锁，保护任务队列 */
};

#endif // INKING_BACKEND_FRAMEWORK_TASK_QUEUE_LOOP_H
