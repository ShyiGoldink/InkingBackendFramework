#pragma once

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

struct Row
{
    std::vector<std::string> columns;
};
/**
 * @brief
 * 数据库返回的数据结构，有快捷打印功能，也可以直接拿到string
 */
struct QueryResult
{
    bool success = false;
    std::string errorMessage;
    int affectedRows = 0;
    std::vector<Row> rows;

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