#ifndef INKING_BACKEND_FRAMEWORK_BASIC_SHINE_LOG_H
#define INKING_BACKEND_FRAMEWORK_BASIC_SHINE_LOG_H

#include <string>
#include <mutex>

/**
 * @brief ShineBasicModule 使用的静态日志工具。
 *
 * 存放位置：可执行文件同级的 LOG/ 目录下，一天一个文件夹，一次运行一个网页：
 *
 *   <可执行文件目录>/LOG/<年-月-日>/<时-分-秒>.html
 *
 * 这样单文件不会无限增长：翻起来按天归类，出问题先定位到哪一天、哪一次运行。
 * 文件名撞车时（同一秒起了两个进程）自动加 -1、-2 后缀。
 * 程序跨零点还在跑的话，写到新的一天时会自动切到新文件夹里的新文件。
 * 页面内部仍按“天”和“启动会话”分组，样式与之前一致。
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

private:
    /**
     * @brief 日志颜色类型。
     */
    enum class Color
    {
        Black,
        Green,
        Red
    };
    /**
     * @brief 新建一份日志文件并写页面骨架、当天标题和本次会话标题。
     *
     * 目录不存在就创建；文件重名就顺延编号。
     * 成功后 _currentDay / _currentFile 指向这份新文件。
     *
     * @param day 形如 2026-09-21 的日期，决定放进哪个文件夹。
     * @param stamp 形如 2026-09-21 13:55:02 的时间戳，用于文件名和会话标题。
     */
    static void openLogFile(const std::string &day, const std::string &stamp);

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
     * @brief 日志根目录。
     * @return 可执行文件同级的 LOG 目录；取不到可执行文件路径时回退到当前目录下的 LOG。
     */
    static std::string logRootDirectory();

    /**
     * @brief 转义 HTML 特殊字符。
     * @param text 原始文本。
     * @return 可以安全写入 HTML 文本节点的字符串。
     */
    static std::string escapeHtml(const std::string &text);

    /** @brief 串行化日志写入，避免多线程并发写坏文件。 */
    static std::mutex _mutex;
    /** @brief 当前日志文件属于哪一天，形如 2026-09-21。 */
    static std::string _currentDay;
    /** @brief 当前正在追加的日志文件全路径；为空表示还没写过日志。 */
    static std::string _currentFile;
};

#endif // INKING_BACKEND_FRAMEWORK_BASIC_SHINE_LOG_H
