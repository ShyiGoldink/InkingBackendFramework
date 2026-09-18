#include "thread/ThreadPool.h"
#include "basic/ShineLog.h"

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
    //线程数组是定长的，重复初始化会把已经跑起来的线程对象覆盖掉，
    //那些线程再也没人 join，析构时直接 terminate。这里挡掉第二次。
    if(_threads[0]){
        setStageStatus(STAGE_INIT_THREAD_POOL,"创建线程池",false,"线程池已经初始化过，忽略这次重复初始化");
        return;
    }
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
        //每一轮都在锁内先看一次"有没有活"，再决定睡不睡。
        //
        //这个顺序不只是为了少睡一次：谓词读的是别处的状态（比如任务队列），
        //保护它的锁和这把 _mutex 不是同一把。通知方 wake()/wakeAll() 会先取一次
        //_mutex 再通知，于是"判断完谓词、还没阻塞"这段窗口里到达的通知，
        //要么已经被这次判断看见，要么让本线程已经在条件变量上排好队等着被叫，
        //两种情况都不会丢通知。
        //
        //反过来，"先睡一觉、醒了再看"就要为每条已经到手的任务先付出一次睡眠，
        //并且每次被叫醒都要重新判断一遍谓词，白跑一轮。
        std::unique_lock<std::mutex> lock(_mutex);
        if (_isQuit)
        {
            break;
        }
        //有活就立刻干，绝不为一件已经到手的活先睡一觉。抢不到活（队列刚被
        //别的管家线程清空）就回循环顶部重新判断，也是往下走而不是硬睡。
        if (_predicateCallback())
        {
            lock.unlock();
            executeGuarded();
            continue;
        }
        if (_waitHintCallback)
        {
            // 带超时地等：延迟任务到点时不会有人 notify，只能靠超时醒来。
            //
            // 这里故意用不带谓词的那个重载。带谓词的 wait_for(lock, 时长, 谓词)
            // 在被通知之后会"复用同一个时长"：条件还不成立就拿着旧时长再睡一轮。
            // 那样一来，新任务入队的通知只会让线程把原来那段长觉重新睡满
            // （比如本来算出来可以睡 1 小时，通知到达后它又睡 1 小时）。
            //
            // 睡多久必须在锁内、并且在上面那次判断之后重新算：退出锁再算，
            // 算出来的可能是别人已经处理过的旧状态。
            const auto hint = _waitHintCallback();
            if (hint <= std::chrono::milliseconds::zero())
            {
                //刚判完就有活到期，或者刚算完就被别人抢空，回顶部重新判断，
                //不在这里睡，也不在这里空转（顶部会立刻取到任务或算出新的时长）。
                continue;
            }
            _cv.wait_for(lock, hint);
        }
        else
        {
            // 没有定时需求就睡到有人唤醒；被唤醒后由循环顶部重新判断谓词，
            // 条件变量的虚假唤醒也交给这个循环消化。
            _cv.wait(lock);
        }
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
    _cv.notify_one(); // 一条任务就叫一个人，不做全员唤醒
}

void ThreadPool::wakeAll(){
    {
        std::lock_guard<std::mutex> lock(_mutex);
    }
    _cv.notify_all();
}

void ThreadPool::executeGuarded(){
    try{
        _executeCallback();
    }catch(const std::exception &exception){
        ShineLog::error("ThreadPool",std::string("管家线程执行任务抛出异常：")+exception.what());
    }catch(...){
        ShineLog::error("ThreadPool","管家线程执行任务抛出未知异常");
    }
}

ThreadPool::~ThreadPool(){
    quit();
    for(size_t i =0;i<THREADNUM;i++){
        if(_threads[i] && _threads[i]->joinable()){
            _threads[i]->join();
        }
    }
}
