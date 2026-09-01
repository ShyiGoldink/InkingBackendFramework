#ifndef INKING_BACKEND_FRAMEWORK_UI_MESSAGE_LIBRARY_H
#define INKING_BACKEND_FRAMEWORK_UI_MESSAGE_LIBRARY_H

#include "dataStruct/MessageStruct.h"

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
    /**添加消息,messageType表示消息类型,delayTime表示等待时间,message为消息内容*/
    static void addMessage(const MessageType &messageType, const float &delayTime, const std::string &message);
    /**快捷添加消息，通过bool快捷决定消息类型 */
    static void quickMessage(const bool &success, const float &delayTime, const std::string &message);
    /**消息输出，如果存在消息就返回true,否则返回false */
    static bool popMessage();
    /**取出当前所有待处理消息，供 UI 线程统一重绘/输出 */
    static std::vector<Message> drainMessages();
    /**等待消息到达，供 UI 线程阻塞等待新事件 */
    static void waitForMessage(std::chrono::milliseconds timeout);

private:
    UIMessageLibrary();
    ~UIMessageLibrary();
    static std::function<void()> _callbackFunction;
    static std::mutex _mutex;
    static std::condition_variable _condition;
    static std::queue<Message> _messageQueue;
};

#endif // INKING_BACKEND_FRAMEWORK_UI_MESSAGE_LIBRARY_H
