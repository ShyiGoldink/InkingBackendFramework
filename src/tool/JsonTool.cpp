#include "tool/JsonTool.h"
#include "ui/UIMessageLibrary.h"

#include <fstream>
#include <iomanip>
#include <iostream>

std::optional<JsonTool::Json> JsonTool::loadFromFile(const std::string &filePath) const
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        const std::string text = "无法打开JSON文件: " + filePath;
        const char *msg = text.c_str();
        UIMessageLibrary::quickMessage(false, 0.0f, msg);
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
        const std::string text = std::string("JSON解析失败: ") + error.what();
        const char *msg = text.c_str();
        UIMessageLibrary::quickMessage(false, 0.0f, msg);
        return std::nullopt;
    }
}

bool JsonTool::saveToFile(const std::string &filePath, const Json &json, int indent) const
{
    std::ofstream file(filePath);
    if (!file.is_open())
    {
        const std::string text = "无法写入JSON文件: " + filePath;
        const char *msg = text.c_str();
        UIMessageLibrary::quickMessage(false, 0.0f, msg);
        return false;
    }

    file << std::setw(indent) << json << std::endl;
    return true;
}
