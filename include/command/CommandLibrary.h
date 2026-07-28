#ifndef INKING_BACKEND_FRAMEWORK_COMMAND_LIBRARY_H
#define INKING_BACKEND_FRAMEWORK_COMMAND_LIBRARY_H

#include "dataStruct/CommandStruct.h"

/**
 * @brief
 * 这里虽然不是静态类，但实际上就是一个command仓库
 * 这里会include所有要提供方法的类
 * 并且写好command，并提供接口让某个类获取到此类，来向commandcenter中注册
 */
class CommandLibrary
{
public:
    std::vector<Command> getCommands()
    {
        return commands;
    }

private:
    std::vector<Command> commands = {
        {}};
};

#endif // INKING_BACKEND_FRAMEWORK_COMMAND_LIBRARY_H