#ifndef INKING_BACKEND_FRAMEWORK_MESSAGE_STRUCT_H
#define INKING_BACKEND_FRAMEWORK_MESSAGE_STRUCT_H

#include <string>

enum MessageType
{
    normal = 0,
    error = 1,
    pass = 2,
};

/**
 * @brief 用于包装控制台消息的输出队列
 * @param
 */
struct Message
{
    MessageType type;
    float delayTime;/** 延迟多少秒之后才输出，0 表示立即。延迟由任务队列负责，见 UIMessageLibrary::addMessage */
    std::string message;
};

#endif // INKING_BACKEND_FRAMEWORK_MESSAGE_STRUCT_H
