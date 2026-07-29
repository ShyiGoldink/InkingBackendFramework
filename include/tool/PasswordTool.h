#ifndef INKING_BACKEND_FRAMEWORK_PASSWORD_TOOL_H
#define INKING_BACKEND_FRAMEWORK_PASSWORD_TOOL_H

#include <string>
#include <string_view>

/**
 * @brief
 * 密码小工具
 * 使用静态属性，在编译时储存hash值作为密码验证
 */
class PasswordTool
{
public:
    PasswordTool() = default;
    ~PasswordTool() = default;
    /**从控制台获取数据 */
    bool verifyConsole() const;

private:
    /**实际检验密码是否正确的方法*/
    bool verify(const std::string &password) const;
    /**根据用户的输入派生内部校验hash */
    std::string deriveHash(const std::string &password) const;
    /**逐位对比hash值，防止使用“==”导致提前退出的时间差 */
    bool fixedTimeEquals(const std::string &left, const std::string &right) const;
    static constexpr std::string_view kSaltPart1 = "shyi";                                /**salt 1 */
    static constexpr std::string_view kSaltPart2 = "qi";                                  /**salt 2 */
    static constexpr std::string_view kExpectedHash = "cb9788682631d9044aadeee6032a4908"; /**实际密码储存hash值 */
};

#endif // INKING_BACKEND_FRAMEWORK_PASSWORD_TOOL_H
