#include "ShineLog.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

bool ShineLog::_checked = false;

void ShineLog::write(const std::string& moduleName, const std::string& message)
{
    writeLine(moduleName, message, Color::Black);
}

void ShineLog::pass(const std::string& moduleName, const std::string& message)
{
    writeLine(moduleName, message, Color::Green);
}

void ShineLog::error(const std::string& moduleName, const std::string& message)
{
    writeLine(moduleName, message, Color::Red);
}

void ShineLog::blue(const std::string& moduleName, const std::string& message)
{
    writeLine(moduleName, message, Color::Blue);
}

void ShineLog::ensureLogFile()
{
    if (_checked) {
        return;
    }

    const std::string path = logFilePath();
    std::ifstream input(path);
    if (!input.good()) {
        std::ofstream output(path);
        output
            << "<!doctype html>\n"
            << "<html lang=\"zh-CN\">\n"
            << "<head>\n"
            << "    <meta charset=\"UTF-8\">\n"
            << "    <title>InkingBackendFramework Log</title>\n"
            << "    <style>\n"
            << "        body { font-family: Menlo, Consolas, monospace; line-height: 1.6; }\n"
            << "        .time { color: black; }\n"
            << "        .black { color: black; }\n"
            << "        .green { color: green; }\n"
            << "        .red { color: red; }\n"
            << "        .blue { color: blue; }\n"
            << "    </style>\n"
            << "</head>\n"
            << "<body>\n"
            << "<h1>InkingBackendFramework Log</h1>\n";
    }

    _checked = true;
}

void ShineLog::writeLine(const std::string& moduleName, const std::string& message, Color color)
{
    ensureLogFile();

    std::ofstream output(logFilePath(), std::ios::app);
    output
        << "<p>"
        << "<span class=\"time\">"
        << "[" << escapeHtml(nowTime()) << "]"
        << "</span> "
        << "<span class=\"" << colorName(color) << "\">"
        << "[" << escapeHtml(moduleName) << "] "
        << escapeHtml(message)
        << "</span>"
        << "</p>\n";
}

std::string ShineLog::colorName(Color color)
{
    switch (color) {
    case Color::Black:
        return "black";
    case Color::Green:
        return "green";
    case Color::Red:
        return "red";
    case Color::Blue:
        return "blue";
    }

    return "black";
}

std::string ShineLog::nowTime()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime {};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

std::string ShineLog::logFilePath()
{
#ifdef _WIN32
    std::vector<char> buffer(MAX_PATH);
    const DWORD size = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0) {
        return (std::filesystem::path(buffer.data()).parent_path() / "Log.html").string();
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return (std::filesystem::weakly_canonical(std::filesystem::path(buffer.data())).parent_path() / "Log.html").string();
    }
#else
    std::vector<char> buffer(4096);
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size > 0) {
        buffer[static_cast<std::size_t>(size)] = '\0';
        return (std::filesystem::path(buffer.data()).parent_path() / "Log.html").string();
    }
#endif

    return "Log.html";
}

std::string ShineLog::escapeHtml(const std::string& text)
{
    std::string escaped;
    escaped.reserve(text.size());

    for (const char item : text) {
        switch (item) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&#39;";
            break;
        default:
            escaped += item;
            break;
        }
    }

    return escaped;
}
