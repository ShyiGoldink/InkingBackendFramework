#include "ui/UIMessageLibrary.h"

std::function<void()> UIMessageLibrary::_callbackFunction = nullptr;
std::queue<Message> UIMessageLibrary::_messageQueue = {};

void UIMessageLibrary::addMessage(const MessageType &messageType, const float &delayTime, const std::string &message)
{
    if (message.empty())
    {
        return;
    }

    static std::string formattedMessage;

    const char *prefix = "\033[33mInking> \033[0m";
    const char *suffix = "";

    switch (messageType)
    {
    case MessageType::error:
        prefix = "\033[31m[ERROR] \033[0m";
        suffix = "\033[0m";
        break;
    case MessageType::pass:
        prefix = "\033[32m[PASS] \033[0m";
        suffix = "\033[0m";
        break;
    case MessageType::normal:
    default:
        prefix = "\033[33mInking> \033[0m";
        suffix = "";
        break;
    }

    formattedMessage = std::string(prefix) + message + suffix;

    Message m{
        messageType, delayTime, message};

    _messageQueue.push(m);
}

void UIMessageLibrary::quickMessage(const bool &success, const float &delayTime, const std::string &message)
{
    if (success)
        addMessage(MessageType::pass, delayTime, message);
    else
        addMessage(MessageType::error, delayTime, message);
}

void UIMessageLibrary::regnsisCallback(std::function<void()> callback)
{
    if (_callbackFunction == nullptr)
    {
        _callbackFunction = callback;
    }
}