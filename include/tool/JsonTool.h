#ifndef INKING_BACKEND_FRAMEWORK_JSON_TOOL_H
#define INKING_BACKEND_FRAMEWORK_JSON_TOOL_H

#include <optional>
#include <string>

#include <nlohmann/json.hpp>
/**
 * @brief
 * JsonTool是一个Json小工具，主要功能就是安全地读取/写入Json；
 * 你只需要传入文件地址和需要的对象名称，就能返回你想要的数据；
 * 或者是传入文件地址以及想要写入的对象名称和数据，就能写入你想要的数据。
 */
class JsonTool
{
public:
    JsonTool() = default;
    ~JsonTool() = default;
    using Json = nlohmann::json;

    std::optional<Json> loadFromFile(const std::string &filePath) const;
    bool saveToFile(const std::string &filePath, const Json &json, int indent = 4) const;

    /**从已有的JSON对象中读取指定key的值 */
    template <typename T>
    std::optional<T> getValue(const Json &json, const std::string &key) const
    {
        if (!json.contains(key) || json.at(key).is_null())
            return std::nullopt;
        try
        {
            return json.at(key).get<T>();
        }
        catch (const nlohmann::json::exception &)
        {
            return std::nullopt;
        }
    }
    /**读取指定文件的指定key的数据,重载的快捷方法 */
    template <typename T>
    std::optional<T> getValue(const std::string &filePath, const std::string &key) const
    {
        const auto json = loadFromFile(filePath);
        if (!json.has_value())
            return std::nullopt;
        return getValue<T>(*json, key);
    }
    /**从已有的JSON对象中写入指定Key的值 */
    template <typename T>
    bool saveValue(Json &json, const std::string &key, const T &value) const
    {
        json[key] = value;
        return true;
    }
    /**将数据写入指定文件的指定key之中，重载的快捷方法 */
    template <typename T>
    bool saveValue(const std::string &filePath, const std::string &key, const T &value, int indent = 4) const
    {
        auto json = loadFromFile(filePath);
        if (!json.has_value())
            return false;
        (*json)[key] = value;
        return saveToFile(filePath, *json, indent);
    }
};

#endif // INKING_BACKEND_FRAMEWORK_JSON_TOOL_H
