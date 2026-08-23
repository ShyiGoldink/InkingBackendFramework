#ifndef INKING_BACKEND_FRAMEWORK_COMMAND_LIBRARY_H
#define INKING_BACKEND_FRAMEWORK_COMMAND_LIBRARY_H

#include "dataStruct/CommandStruct.h"
#include "basic/ShineStatusChecker.h"

/**
 * @brief
 * 命令库。
 *
 * 负责把具体模块的 public 方法包装成 Command。
 * 头文件只暴露命令获取接口，具体模块依赖放在 .cpp 中。
 */
class CommandLibrary
{
public:
    /**获取全部命令 */
    std::vector<std::vector<Command>> commands() const;
    /**获取数据库相关命令 */
    std::vector<Command> databaseCommands() const;
    /**获取UI界面相关命令 */
    std::vector<Command> uiCommands() const;
};

#endif // INKING_BACKEND_FRAMEWORK_COMMAND_LIBRARY_H
