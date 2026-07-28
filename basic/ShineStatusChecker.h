#ifndef INKING_BACKEND_FRAMEWORK_BASIC_SHINE_STATUS_CHECKER_H
#define INKING_BACKEND_FRAMEWORK_BASIC_SHINE_STATUS_CHECKER_H

#include <string>
#include <unordered_map>
#include <vector>

class ShineBasicModule;

/**
 * @brief
 * 此类是用于基类注册指针
 * 注册指针后，其它模块就能从此类中获取到对应的指针
 * 并通过指针提供的模块名和公开方法，实现各种命令的实现
 */
class ShineStatusChecker
{
public:
    ShineStatusChecker() = default;
    virtual ~ShineStatusChecker() = default;

    /**在statusCheker中注册指针，用于后续调用 */
    static void registerCallbackPointer(const std::string &moduleName, ShineBasicModule *module);
    /**在statusCheker中注销指针 */
    static void executeCallbackPointer(const std::string &moduleName, ShineBasicModule *module);
    /**提供全部到key值，用于后续的UIManager获取到所有最新的指令 */
    static std::vector<std::string> getAllModuleNames();
    /**根据传入的string也就是key值，获取对应的module指针列表 */
    static std::vector<ShineBasicModule *> getModules(const std::string &moduleName);

private:
    /** 储存basic基类对指针用于获取“获取stage数据的函数”*/
    static std::unordered_map<std::string, std::vector<ShineBasicModule *>> _callbackPointers;
};

#endif // INKING_BACKEND_FRAMEWORK_BASIC_SHINE_STATUS_CHECKER_H
