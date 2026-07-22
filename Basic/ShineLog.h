#ifndef INKING_BACKEND_FRAMEWORK_BASIC_SHINE_LOG_H
#define INKING_BACKEND_FRAMEWORK_BASIC_SHINE_LOG_H

#include <string>

/**
 * @brief ShineBasicModule 使用的静态日志工具。
 *
 * 日志会写入可执行文件同级目录下的 Log.html。
 * 第一次写入时会检查文件是否存在；如果不存在，就创建并写入标题。
 */
class ShineLog
{
public:
    /**
     * @brief 写入一条普通日志。
     * @param moduleName 产生日志的模块名称。
     * @param message 日志内容。
     */
    static void write(const std::string &moduleName, const std::string &message);

    /**
     * @brief 写入一条通过日志。
     * @param moduleName 产生日志的模块名称。
     * @param message 日志内容。
     */
    static void pass(const std::string &moduleName, const std::string &message);

    /**
     * @brief 写入一条错误日志。
     * @param moduleName 产生日志的模块名称。
     * @param message 日志内容。
     */
    static void error(const std::string &moduleName, const std::string &message);

    /**
     * @brief 写入一条蓝色日志。
     * @param moduleName 产生日志的模块名称。
     * @param message 日志内容。
     */
    static void blue(const std::string &moduleName, const std::string &message);

private:
    /**
     * @brief 日志颜色类型。
     */
    enum class Color
    {
        Black,
        Green,
        Red,
        Blue
    };
    /**
     * @brief 确认日志文件存在。
     *
     * 该方法只在第一次写入日志时检查一次。
     * 如果可执行文件同级目录下没有 Log.html，就创建基础 HTML 页面。
     */
    static void ensureLogFile();

    /**
     * @brief 向日志文件追加一行带颜色的日志。
     * @param moduleName 产生日志的模块名称。
     * @param message 日志内容。
     * @param color 日志内容颜色；时间戳始终使用黑色。
     */
    static void writeLine(const std::string &moduleName, const std::string &message, Color color);

    /**
     * @brief 将颜色枚举转换成 CSS class 名称。
     * @param color 日志颜色枚举。
     * @return 可写入 HTML class 属性的颜色名称。
     */
    static std::string colorName(Color color);

    /**
     * @brief 获取当前本地时间字符串。
     * @return 格式为 YYYY-MM-DD HH:MM:SS 的时间。
     */
    static std::string nowTime();

    /**
     * @brief 获取日志文件路径。
     * @return 可执行文件同级目录下的 Log.html 路径；如果获取失败，回退到当前目录 Log.html。
     */
    static std::string logFilePath();

    /**
     * @brief 转义 HTML 特殊字符。
     * @param text 原始文本。
     * @return 可以安全写入 HTML 文本节点的字符串。
     */
    static std::string escapeHtml(const std::string &text);

    static bool _checked;
};

#endif // INKING_BACKEND_FRAMEWORK_BASIC_SHINE_LOG_H
