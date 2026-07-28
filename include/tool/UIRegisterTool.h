#ifndef INKING_BACKEND_FRAMEWORK_UI_REGISTER_TOOL_H
#define INKING_BACKEND_FRAMEWORK_UI_REGISTER_TOOL_H

class CommandCenter;

class UIRegisterTool
{
public:
    UIRegisterTool() = default;
    ~UIRegisterTool();
    /**初始化，加载需要的对象指针 */
    bool init(CommandCenter &commandCenter);
    /**注册基础指令 */
    bool registerBasicCommand();

private:
    CommandCenter *_commandCenter = nullptr; /**从持有者获取到的commandCenter对象的指针 */
};
#endif // INKING_BACKEND_FRAMEWORK_UI_REGISTER_TOOL_H