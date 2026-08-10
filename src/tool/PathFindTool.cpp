#include <vector>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif // 根据不同的系统引入不同的文件

#include "tool/PathFindTool.h"

std::filesystem::path PathFindTool::executableDir()
{
#ifdef _WIN32
    std::vector<char> buffer(MAX_PATH);
    const DWORD size = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0)
    {
        return std::filesystem::path(std::string(buffer.data(), size)).parent_path();
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);

    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0)
    {
        return std::filesystem::path(buffer.data()).parent_path();
    }
#else
    std::vector<char> buffer(4096);
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size > 0)
    {
        buffer[static_cast<std::size_t>(size)] = '\0';
        return std::filesystem::path(buffer.data()).parent_path();
    }
#endif

    return std::filesystem::current_path();
}

std::filesystem::path PathFindTool::configPath(const std::string &fileName)
{
    return executableDir() / "config" / fileName;
}