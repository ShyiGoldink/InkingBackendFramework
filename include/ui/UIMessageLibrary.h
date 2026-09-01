#ifndef INKING_BACKEND_FRAMEWORK_UI_MESSAGE_LIBRARY_H
#define INKING_BACKEND_FRAMEWORK_UI_MESSAGE_LIBRARY_H

#include "dataStruct/MessageStruct.h"

#include <queue>
#include <functional>
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
    /**注册回调函数，用于修改UIThread的状态 */
    static void regnsisCallback(std::function<void()>);

private:
    UIMessageLibrary();
    ~UIMessageLibrary();
    static std::queue<Message> _messageQueue;
    static std::function<void()> _callbackFunction;
};

#endif // INKING_BACKEND_FRAMEWORK_UI_MESSAGE_LIBRARY_H
