#include "thread/ThreadPool.h"

namespace
{   constexpr int STAGE_INIT_THREAD_POOL = 1;
    constexpr int STAGE_REGISTER_BUTLER = 2;
}

ThreadPool::ThreadPool(){
    //因为不太可能创建对象就直接产程竞态，所以这个标志位不上锁
   _stage = STAGE_REGISTER_BUTLER+10;
   registerToStatusChecker();
}

void ThreadPool::init(std::function<bool()> predicate, std::function<void()> execute,
                      std::function<std::chrono::milliseconds()> waitHint){
    if(predicate)_predicateCallback = predicate;
    if(execute)_executeCallback = execute;
    if(waitHint)_waitHintCallback = waitHint;
     for(size_t i =0;i<THREADNUM;i++){
        //创建线程
        //然后让线程执行管家函数
         _threads[i] = std::make_unique<std::thread>(&ThreadPool::butler, this);
         _stage++;
    }
    setStageStatus(STAGE_INIT_THREAD_POOL,"创建线程池",true,"线程池创建完成");
}

void ThreadPool::butler(){
    if(!_predicateCallback ||! _executeCallback){
        setStageStatus(STAGE_REGISTER_BUTLER+_stage,"向线程中注入管家函数",false,"未成功传入谓词函数或执行函数");
        return;
    }
    if(_isQuit){
        setStageStatus(STAGE_REGISTER_BUTLER+_stage,"向线程中注入管家函数",false,"线程池退出！");
        return;
    }
    setStageStatus(STAGE_REGISTER_BUTLER+_stage,"向线程中注入管家函数",true,"已创建线程管家");
    //管家函数，处理循环
    while (true)
    {
        if (_isQuit)
        {
            break;
        }
        std::unique_lock<std::mutex> lock(_mutex);
        // 使用条件变量等待，避免CPU空转。
        // 唤醒条件：退出标志已置位，或谓词（例如“队列非空”）成立。
        // 必须把 _isQuit 纳入条件，否则 quit() 后 join() 会永久阻塞。
        const auto ready = [this]()
        { return _isQuit || (_predicateCallback && _predicateCallback()); };
        if (_waitHintCallback)
        {
            // 带超时地等：延迟任务到点时不会有人 notify，只能靠超时醒来。
            //
            // 这里故意用不带谓词的那个重载。带谓词的 wait_for(lock, 时长, 谓词)
            // 在被通知之后会"复用同一个时长"：条件还不成立就拿着旧时长再睡一轮。
            // 那样一来，新任务入队的通知只会让线程把原来那段长觉重新睡满
            // （比如本来算出来可以睡 1 小时，通知到达后它又睡 1 小时）。
            // 不带谓词则一定会返回，回到循环顶部重新计算该睡多久。
            _cv.wait_for(lock, _waitHintCallback());
        }
        else
        {
            _cv.wait(lock, ready);
        }
        if (_isQuit)
        {
            break;
        }
        // 超时或被唤醒之后再看一眼是不是真该干活了；
        // 不该干就回到循环顶部重新算等待时长，而不是继续睡原来那一段。
        if (_waitHintCallback && !_predicateCallback())
        {
            continue;
        }
        lock.unlock();
        _executeCallback();
    }
}

void ThreadPool::quit(){
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _isQuit = true;
    }
    _cv.notify_all(); // 唤醒所有等待的线程
}

void ThreadPool::wake(){
    // 这里先空取一次 _mutex 再通知，不是多余的。
    // 条件变量用的是这把 _mutex，但谓词读的状态（任务队列）在 TaskQueueLoop 手里，
    // 由 TaskQueueLoop 自己的锁保护。如果通知方完全不碰 _mutex，就可能出现
    // “任务已经入队、通知却落在等待方判断完谓词到真正阻塞之间”的丢唤醒，
    // 管家线程会一直睡下去。取一次锁能把通知和阻塞排好序。
    {
        std::lock_guard<std::mutex> lock(_mutex);
    }
    _cv.notify_all(); // 有新任务入队时唤醒管家线程
}

ThreadPool::~ThreadPool(){
    quit();
    for(size_t i =0;i<THREADNUM;i++){
        if(_threads[i] && _threads[i]->joinable()){
            _threads[i]->join();
        }
    }
}
