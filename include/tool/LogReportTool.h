#ifndef INKING_BACKEND_FRAMEWORK_TOOL_LOG_REPORT_TOOL_H
#define INKING_BACKEND_FRAMEWORK_TOOL_LOG_REPORT_TOOL_H

#include <string>

/**
 * @brief 把纯文本日志转成一份可浏览的 HTML 报告。
 *
 * 运行期不再产出 HTML：日志本体是 `.log`，报告是"需要时"由这个工具从 `.log`
 * 生成的派生产物。生成的报告是自包含单文件——日志原文内联在页面里，
 * 双击即可打开，不依赖本地服务器，也不会被浏览器的 file:// 跨域策略拦住。
 */
class LogReportTool
{
public:
    /**
     * @brief 从日志文件生成报告。
     * @param logPath 日志文件路径。
     * @return 生成的报告路径；失败时返回空串。
     */
    static std::string generateFromFile(const std::string &logPath);
};

#endif // INKING_BACKEND_FRAMEWORK_TOOL_LOG_REPORT_TOOL_H
