#pragma once

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

/**
 * @brief
 *数据库的配置信息
 * @param host 数据库的服务器地址(在哪一台机器上)
 * @param port 数据库端口
 * @param userName 用户名
 * @param password 数据库密码
 * @param databaseName 数据库名
 */
struct DatabaseConfig
{
    std::string host;
    int port;
    std::string userName;
    std::string password;
    std::string databaseName;
};

enum class ErrorKind
{
    None =0,/**成功，无错误 */
    Statement,/**语句级别的错误，例如语法，约束，权限，表不存在等 */
    Connection,/**连接级的错误，例如网络断开，服务端关闭，读写超时，连接必须丢弃 */
    Local,/**本地错误：尚未连接，参数不合法等，与网络无关 */
    
};

struct Row
{
    std::vector<std::string> columns;
};
/**
 * @brief
 * 数据库返回的数据结构，有快捷打印功能，也可以直接拿到string
 * @param success query内容是否成功
 * @param errorMessage 提示错误信息
 * @param affectedRows 影响的行数据
 * @param rows 实际数据
 */
struct QueryResult
{
    bool success = false;
    std::string errorMessage;
    ErrorKind errorKind = ErrorKind::None;/**错误分类 */
    uint32_t errorCode = 0;/**原生错误码 */
    uint64_t affectedRows = 0;
    std::vector<Row> rows;

        /** 本次失败是不是连接级错误（连接还能不能继续用） */
    bool connectionUsable() const { return errorKind != ErrorKind::Connection; }

    /**给出内容 */
    std::string toString() const
    {
        if (!success)
            return "Error: " + errorMessage;

        if (rows.empty())
        {
            return "OK, " + std::to_string(affectedRows) +
                   " row(s) affected.";
        }

        // 计算总列数
        size_t columnCount = 0;
        for (const auto &row : rows)
            columnCount = std::max(columnCount, row.columns.size());

        // 计算每列最大宽度
        std::vector<size_t> widths(columnCount, 0);

        for (const auto &row : rows)
        {
            for (size_t i = 0; i < row.columns.size(); ++i)
            {
                widths[i] = std::max(
                    widths[i],
                    row.columns[i].size());
            }
        }

        std::string result;

        const auto drawLine = [&]()
        {
            for (size_t width : widths)
                result += "+-" + std::string(width, '-') + "-";

            result += "+\n";
        };

        drawLine();

        for (const auto &row : rows)
        {
            for (size_t i = 0; i < columnCount; ++i)
            {
                const std::string value =
                    i < row.columns.size()
                        ? row.columns[i]
                        : "";

                result += "| ";
                result += value;
                result += std::string(
                    widths[i] - value.size(),
                    ' ');
                result += " ";
            }

            result += "|\n";
            drawLine();
        }

        return result;
    }
    /**快捷打印，不过因为引入了Message线程，所以暂时没用了 */
    void printResult(std::ostream &out) const
    {
        if (!success)
        {
            constexpr const char *RED = "\033[31m";
            constexpr const char *RESET = "\033[0m";

            out << RED << toString() << RESET << '\n';
            return;
        }
        else
        {
            out << "success!" << '\n';
        }

        out << toString() << '\n';
    }
};
