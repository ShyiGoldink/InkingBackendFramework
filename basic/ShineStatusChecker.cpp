#include "ShineStatusChecker.h"

#include <algorithm>

// 静态变量初始化
std::unordered_map<std::string, std::vector<ShineBasicModule *>> ShineStatusChecker::_callbackPointers;

// 注册指针
void ShineStatusChecker::registerCallbackPointer(const std::string &moduleName, ShineBasicModule *module)
{
    if (module)
    {
        auto &modules = _callbackPointers[moduleName];
        // 先查重，如果不存在才进行注册
        if (std::find(modules.begin(), modules.end(), module) == modules.end())
        {
            modules.push_back(module);
        }
    }
}
// 注销指针
void ShineStatusChecker::discuteCallbackPointer(const std::string &moduleName, ShineBasicModule *module)
{
    if (module) // 鲁棒设计
    {
        auto it = _callbackPointers.find(moduleName); // 找到key下的vector
        if (it != _callbackPointers.end())            // 找到了
        {
            auto &modules = it->second;                                                        // 获取vectors
            modules.erase(std::remove(modules.begin(), modules.end(), module), modules.end()); // 移除注销的指针

            if (modules.empty()) // 如果这个key下不存在任何值，进行更新，将key移除
            {
                _callbackPointers.erase(it);
            }
        }
    }
}
// 获取所有的key值
std::vector<std::string> ShineStatusChecker::getAllModuleNames()
{
    std::vector<std::string> moduleNames;
    for (const auto &pair : _callbackPointers)
    {
        moduleNames.push_back(pair.first);
    }
    return moduleNames;
}
// 根据传入的key值，获取需要的module指针列表
std::vector<ShineBasicModule *> ShineStatusChecker::getModules(const std::string &moduleName)
{
    auto it = _callbackPointers.find(moduleName);
    if (it != _callbackPointers.end())
    {
        return it->second;
    }
    return {};
}