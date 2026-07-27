#ifndef INKING_BACKEND_FRAMEWORK_TOOL_FUZZY_MATCHER_H
#define INKING_BACKEND_FRAMEWORK_TOOL_FUZZY_MATCHER_H

#include <optional>
#include <string>
#include <vector>

/**
 * @brief 轻量级模糊匹配工具。
 *
 * 当前使用编辑距离匹配命令名称。
 * 适合控制台命令数量较少的场景，不依赖额外库。
 */
class FuzzyMatcher
{
public:
    /**
     * @brief 从候选项中寻找最接近输入文本的一项。
     * @param input 用户输入。
     * @param candidates 可匹配的命令名或别名。
     * @param maxDistance 最大允许编辑距离。
     * @return 找到时返回候选文本；否则返回 std::nullopt。
     */
    static std::optional<std::string> bestMatch(
        const std::string& input,
        const std::vector<std::string>& candidates,
        std::size_t maxDistance = 2);

private:
    /**
     * @brief 计算两个字符串之间的编辑距离。
     * @param left 左侧字符串。
     * @param right 右侧字符串。
     * @return 将 left 转换成 right 所需的最少插入、删除、替换次数。
     */
    static std::size_t editDistance(const std::string& left, const std::string& right);
};

#endif // INKING_BACKEND_FRAMEWORK_TOOL_FUZZY_MATCHER_H
