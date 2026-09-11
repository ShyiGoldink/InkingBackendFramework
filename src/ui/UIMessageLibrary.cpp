#include "ui/UIMessageLibrary.h"
#include "thread/TaskQueueLoop.h"

#include <any>
#include <chrono>
#include <iostream>
#include <vector>

std::mutex UIMessageLibrary::_mutex;
std::condition_variable UIMessageLibrary::_condition;
std::queue<Message> UIMessageLibrary::_messageQueue = {};

void UIMessageLibrary::addMessage(const MessageType &messageType, const float &delayTime, const std::string &message)
{
    if (message.empty())
    {
        return;
    }

    // 延迟消息不在这里等，而是交给任务队列：
    // 排一个 delay 之后才执行的任务，任务到点做的唯一一件事就是按"立即"重新入队。
    // 这样本类保持成一条笨队列，"等"的能力只需要任务队列有。
    if (delayTime > 0.0f)
    {
        const auto delay = std::chrono::milliseconds(
            static_cast<long long>(static_cast<double>(delayTime) * 1000.0));

        Task<std::any> task;
        task.action = [messageType, message](const std::vector<std::any> &) -> std::any
        {
            addMessage(messageType, 0.0f, message);
            return {};
        };
        TaskQueueLoop::instance().addTask(std::move(task), delay);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _messageQueue.push(Message{messageType, delayTime, message});
    }
    _condition.notify_one();
}

void UIMessageLibrary::quickMessage(const bool &success, const float &delayTime, const std::string &message)
{
    if (success)
        addMessage(MessageType::pass, delayTime, message);
    else
        addMessage(MessageType::error, delayTime, message);
}

std::vector<Message> UIMessageLibrary::drainMessages()
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Message> messages;
    while (!_messageQueue.empty())
    {
        messages.push_back(_messageQueue.front());
        _messageQueue.pop();
    }
    return messages;
}

void UIMessageLibrary::waitForMessage(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _condition.wait_for(lock, timeout, [] { return !_messageQueue.empty(); });
}

size_t UIMessageLibrary::pendingCount()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _messageQueue.size();
}
