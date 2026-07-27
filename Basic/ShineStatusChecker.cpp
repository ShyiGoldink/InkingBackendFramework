#include "ShineStatusChecker.h"
// 静态变量初始化
std::unordered_map<std::string, std::vector<ShineBasicModule *>> ShineStatusChecker::_callbackPointers;
ShineStatusChecker::ShineStatusChecker() {};

void ShineStatusChecker::registerCallback(const std::string &moduleName, ShineBasicModule *module)
{
       if(module)_callbackPointers[moduleName].push_back(module);
}

void ShineStatusChecker::executeCallbacks(const std::string &moduleName, ShineBasicModule *module)
{
    if(module){
        auto it = _callbackPointers.find(moduleName);
        if (it != _callbackPointers.end())
        {
            auto &modules = it->second;
            modules.erase(std::remove(modules.begin(), modules.end(), module), modules.end());
        }
    }
}

ShineStatusChecker::~ShineStatusChecker() {};