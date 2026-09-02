#include "ui/UIMessageLibrary.h"

#include <chrono>
#include <iostream>

std::mutex UIMessageLibrary::_mutex;
std::condition_variable UIMessageLibrary::_condition;
std::queue<Message> UIMessageLibrary::_messageQueue = {};

void UIMessageLibrary::addMessage(const MessageType &messageType, const float &delayTime, const std::string &message)
{
    if (message.empty())
    {
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
