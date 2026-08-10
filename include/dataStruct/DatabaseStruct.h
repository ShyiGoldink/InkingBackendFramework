#pragma once

#include <algorithm>
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
    int affectedRows = 0;
    std::vector<Row> rows;

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
    // 快捷打印
    void printResult(std::ostream &out = std::cout) const
    {
        if (!success)
        {
            constexpr const char *RED = "\033[31m";
            constexpr const char *RESET = "\033[0m";

            out << RED << toString() << RESET << '\n';
            return;
        }

        out << toString() << '\n';
    }
};