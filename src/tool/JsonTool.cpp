#include "tool/JsonTool.h"

#include <fstream>
#include <iomanip>
#include <iostream>

std::optional<JsonTool::Json> JsonTool::loadFromFile(const std::string &filePath) const
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cout << "无法打开JSON文件: " << filePath << std::endl;
        return std::nullopt;
    }

    try
    {
        Json json;
        file >> json;
        return json;
    }
    catch (const nlohmann::json::exception &error)
    {
        std::cout << "JSON解析失败: " << error.what() << std::endl;
        return std::nullopt;
    }
}

bool JsonTool::saveToFile(const std::string &filePath, const Json &json, int indent) const
{
    std::ofstream file(filePath);
    if (!file.is_open())
    {
        std::cout << "无法写入JSON文件: " << filePath << std::endl;
        return false;
    }

    file << std::setw(indent) << json << std::endl;
    return true;
}
