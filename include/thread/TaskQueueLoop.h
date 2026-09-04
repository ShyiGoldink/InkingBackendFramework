#ifndef INKING_BACKEND_FRAMEWORK_TASK_QUEUE_LOOP_H
#define INKING_BACKEND_FRAMEWORK_TASK_QUEUE_LOOP_H

#include <any>
#include <memory>
#include <mutex>
#include <queue>

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
    TaskQueueLoop();
    ~TaskQueueLoop();

    TaskQueueLoop(const TaskQueueLoop &) = delete;
    TaskQueueLoop &operator=(const TaskQueueLoop &) = delete;

    /**添加任务（线程安全），任务会整体（含依赖/后继）在某个管家线程中执行 */
    void addTask(Task<std::any> task);
    /**返回任务队列是否为空（线程安全），供线程池谓词使用：唤醒条件 = 队列非空 */
    bool isEmpty() const;
    /**执行任务：作为线程池的执行函数，从任务队列中取出一个任务并执行 */
    void executeTask();

private:
    std::unique_ptr<ThreadPool> _threadLoop;/**线程池 */
    std::queue<std::unique_ptr<Task<std::any>>> _queue;/**任务队列 */
    mutable std::mutex _mutex;/**锁，保护任务队列 */
};

#endif // INKING_BACKEND_FRAMEWORK_TASK_QUEUE_LOOP_H
