#include "ui/UIMessageLibrary.h"

#include <chrono>
#include <iostream>

std::function<void()> UIMessageLibrary::_callbackFunction = nullptr;
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

bool UIMessageLibrary::popMessage()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_messageQueue.empty())
    {
        return false;
    }

    const Message message = _messageQueue.front();
    _messageQueue.pop();

    switch (message.type)
    {
    case MessageType::error:
        std::cout << "\033[31m[ERROR] \033[0m";
        break;
    case MessageType::pass:
        std::cout << "\033[32m[PASS] \033[0m";
        break;
    case MessageType::normal:
    default:
        std::cout << "\033[33mInking> \033[0m";
        break;
    }

    std::cout << message.message << '\n';
    return true;
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
