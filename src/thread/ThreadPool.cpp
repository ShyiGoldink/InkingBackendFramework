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

void ThreadPool::init(std::function<bool()> predicate, std::function<void()> execute){
    if(predicate)_predicateCallback = predicate;
    if(execute)_executeCallback = execute;
     for(int i =0;i<THREADNUM;i++){
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
        _cv.wait(lock, [this]()
                 { return _isQuit || (_predicateCallback && _predicateCallback()); });
        if (_isQuit)
        {
            break;
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
    _cv.notify_all(); // 有新任务入队时唤醒管家线程
}

ThreadPool::~ThreadPool(){
    quit();
    for(int i =0;i<THREADNUM;i++){
        if(_threads[i] && _threads[i]->joinable()){
            _threads[i]->join();
        }
    }
}
