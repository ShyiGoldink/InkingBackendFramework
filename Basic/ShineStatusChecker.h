#ifndef INKING_BACKEND_FRAMEWORK_BASIC_SHINE_STATUS_CHECKER_H
#define INKING_BACKEND_FRAMEWORK_BASIC_SHINE_STATUS_CHECKER_H

#include <string>
#include <unordered_map>
#include <vector>
#include "ShineBasicModule.h"

class ShineStatusChecker
{
public:
    ShineStatusChecker() = default;
    virtual ~ShineStatusChecker() = default;

    /**在statusCheker中注册指针，用于后续调用 */
    static void registerCallback(const std::string &moduleName, ShineBasicModule *module);
    /**在statusCheker中注销指针 */
    static void executeCallbacks(const std::string &moduleName, ShineBasicModule *module);

private:
    /** 储存basic基类对指针用于获取“获取stage数据的函数”*/
    static std::unordered_map<std::string, std::vector<ShineBasicModule *>> _callbackPointers;
};

#endif // INKING_BACKEND_FRAMEWORK_BASIC_SHINE_STATUS_CHECKER_H