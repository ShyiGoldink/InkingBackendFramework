#ifndef INKING_BACKEND_FRAMEWORK_STATUS_REGIST_TOKEN_H
#define INKING_BACKEND_FRAMEWORK_STATUS_REGIST_TOKEN_H

#include <string>
#include <utility>
#include "ShineStatusChecker.h"

// 声明有这个类，避免循环引用的同时这个类能正常使用
// 因为ShineStatusChecker是静态类，必须使用其中的方法和数据，所以直接include头文件
class ShineBasicModule;
/**
 * @brief
 * 这是基于RAII思想实现的类
 * 目的是为了包装基类的注册函数
 * 将析构函数写入本类的析构中，让对象析构时可以自行析构，这样就能让程序员避免忘记写析构
 */
class StatusRegisterToken
{
public:
    /**在构造函数中直接注册，并且录入对应信息*/
    StatusRegisterToken(const std::string &moduleName, ShineBasicModule *module)
    {
        _moduleName = moduleName;
        _module = module;
        ShineStatusChecker::registerCallbackPointer(_moduleName, _module);
    };
    ~StatusRegisterToken() noexcept
    {
        if (_module && !_moduleName.empty())
        {
            ShineStatusChecker::executeCallbackPointer(_moduleName, _module);
        }
    };
    // 禁止拷贝构造函数和拷贝函数
    StatusRegisterToken(const StatusRegisterToken &) = delete;
    StatusRegisterToken &operator=(const StatusRegisterToken &) = delete;

    StatusRegisterToken(StatusRegisterToken &&other) noexcept
        : _moduleName(std::move(other._moduleName)), _module(other._module)
    {
        other._module = nullptr;
    }

    StatusRegisterToken &operator=(StatusRegisterToken &&other) noexcept
    {
        if (this != &other)
        {
            if (_module && !_moduleName.empty())
            {
                ShineStatusChecker::executeCallbackPointer(_moduleName, _module);
            }

            _moduleName = std::move(other._moduleName);
            _module = other._module;
            other._module = nullptr;
        }

        return *this;
    }

private:
    std::string _moduleName;
    ShineBasicModule *_module = nullptr;
};

#endif // INKING_BACKEND_FRAMEWORK_STATUS_REGIST_TOKEN_H
