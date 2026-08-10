#ifndef INKING_BACKEND_FRAMEWORK_PATH_TOOL_H
#define INKING_BACKEND_FRAMEWORK_PATH_TOOL_H

#include <filesystem>
#include <string>

/**
 * @brief
 *文件路径小工具
 *用户帮助找到正确的路径
 *例如exe路径，便于找到合适的路径 
 */
class PathFindTool{
public:
    /**获取当前可执行文件的所在目录*/
    std::filesystem::path executableDir();
    /**获取可执行文件同级config目录下的文件路径 */
    std::filesystem::path configPath(const std::string &fileName);
};

#endif //INKING_BACKEND_FRAMEWORK_PATH_TOOL_H