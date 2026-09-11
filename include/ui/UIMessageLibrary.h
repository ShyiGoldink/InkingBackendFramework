#ifndef INKING_BACKEND_FRAMEWORK_UI_MESSAGE_LIBRARY_H
#define INKING_BACKEND_FRAMEWORK_UI_MESSAGE_LIBRARY_H

#include "dataStruct/MessageStruct.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>
/**
 *@brife UITread专门管理线程，那么UIMessageLibrary就作为静态类专门管理数据
 */
class UIMessageLibrary
{
public:
    /**
     * 添加消息
     * @param delayTime 延迟多少秒之后才输出，0 表示立即。单位是秒。
     * @note 延迟不是在这里等的：延迟消息会变成任务队列里"delay 之后才执行"的一个任务，
     *       任务到点做的唯一一件事，就是把这条消息按"立即"重新入队。
     *       所以本类只管一条纯粹的队列，不需要懂时间。
     */
    static void addMessage(const MessageType &messageType, const float &delayTime, const std::string &message);
    /**快捷添加消息，通过bool快捷决定消息类型 */
    static void quickMessage(const bool &success, const float &delayTime, const std::string &message);
    /**取出当前所有待处理消息，供 UI 线程统一重绘/输出 */
    static std::vector<Message> drainMessages();
    /**等待消息到达，供 UI 线程阻塞等待新事件 */
    static void waitForMessage(std::chrono::milliseconds timeout);
    /**当前队列里有多少条待处理消息，主要给自检用 */
    static size_t pendingCount();

private:
    UIMessageLibrary();
    ~UIMessageLibrary();
    static std::mutex _mutex;
    static std::condition_variable _condition;
    static std::queue<Message> _messageQueue;
};

#endif // INKING_BACKEND_FRAMEWORK_UI_MESSAGE_LIBRARY_H
