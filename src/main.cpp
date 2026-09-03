#include "core/Application.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main()
{
#ifdef _WIN32
    // 与 README 保持一致：源码与程序输出统一使用 UTF-8。
    // 不设置的话，Windows 控制台默认按 GBK 解码，中文会显示成乱码。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    //这里进入了主线程
    Application app;
    app.run();
    return 0;
}
