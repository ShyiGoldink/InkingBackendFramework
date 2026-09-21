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

std::mutex ShineLog::_mutex;
std::string ShineLog::_currentDay;
std::string ShineLog::_currentFile;

namespace
{
    /** 日志根目录的名字：可执行文件同级的 LOG/ */
    constexpr const char *kLogRootFolder = "LOG";

    std::string directoryOf(const std::string &filePath)
    {
        const std::size_t lastSlash = filePath.find_last_of("/\\");
        if (lastSlash == std::string::npos)
        {
            return ".";
        }

        return filePath.substr(0, lastSlash);
    }

    const char *logPageHeader()
    {
        return R"(<!doctype html>
<!-- INKING-LOG-V2 -->
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="color-scheme" content="dark">
<title>系统日志</title>
<style>
:root{
  --bg:#070b14;
  --line:#1b2a44;
  --cyan:#00e5ff;
  --green:#35f0a6;
  --red:#ff5c7a;
  --dim:#68799b;
  --text:#c9d5ec;
  --panel:#0e1729;
  --panel-strong:#121f34;
}
*{box-sizing:border-box}
body{
  margin:0;
  padding:34px 26px 72px;
  min-height:100vh;
  color:var(--text);
  font-family:"Cascadia Code","JetBrains Mono",Consolas,"Courier New",monospace;
  font-size:14px;
  line-height:1.65;
  background:
    radial-gradient(1000px 520px at 85% -12%, rgba(0,229,255,.09), transparent 60%),
    radial-gradient(760px 420px at -8% 108%, rgba(53,240,166,.07), transparent 55%),
    linear-gradient(rgba(27,42,68,.16) 1px, transparent 1px),
    linear-gradient(90deg, rgba(27,42,68,.16) 1px, transparent 1px),
    var(--bg);
  background-size:auto,auto,38px 38px,38px 38px,auto;
}
header{
  display:flex;
  align-items:baseline;
  gap:16px;
  flex-wrap:wrap;
  padding-bottom:16px;
  margin-bottom:12px;
  border-bottom:1px solid var(--line);
}
h1{
  margin:0;
  font-size:19px;
  font-weight:600;
  letter-spacing:.32em;
  color:var(--cyan);
  text-shadow:0 0 12px rgba(0,229,255,.45);
}
.cursor{
  display:inline-block;
  width:9px;
  height:18px;
  background:var(--cyan);
  box-shadow:0 0 10px var(--cyan);
  animation:blink 1.1s steps(1) infinite;
}
.sub{
  color:var(--dim);
  font-size:11px;
  letter-spacing:.22em;
  text-transform:uppercase;
}
.log-nav{
  position:sticky;
  top:12px;
  z-index:10;
  display:flex;
  align-items:center;
  gap:12px;
  flex-wrap:wrap;
  margin:0 0 20px;
  padding:10px 14px;
  border:1px solid rgba(0,229,255,.24);
  border-radius:12px;
  background:rgba(11,17,29,.9);
  backdrop-filter:blur(8px);
  box-shadow:0 12px 28px rgba(0,0,0,.18);
}
.log-nav-label{
  color:var(--cyan);
  font-size:11px;
  letter-spacing:.18em;
  text-transform:uppercase;
}
.log-nav-list{
  display:flex;
  flex-wrap:wrap;
  gap:8px;
}
.log-nav-item{
  display:inline-flex;
  align-items:center;
  justify-content:center;
  padding:6px 10px;
  border:1px solid rgba(53,240,166,.24);
  border-radius:999px;
  color:var(--text);
  text-decoration:none;
  background:rgba(53,240,166,.04);
  transition:transform .12s ease, border-color .12s ease, background .12s ease;
}
.log-nav-item:hover{
  transform:translateY(-1px);
  border-color:rgba(0,229,255,.6);
  background:rgba(0,229,255,.08);
}
.day-container{
  display:flex;
  flex-direction:column;
  gap:18px;
}
.day-card{
  position:relative;
  display:block;
  padding:16px 18px 14px;
  border:1px solid rgba(27,42,68,.9);
  border-radius:14px;
  background:linear-gradient(180deg, rgba(18,31,52,.9), rgba(10,15,24,.9));
  box-shadow:inset 0 1px rgba(255,255,255,.02), 0 18px 34px rgba(0,0,0,.18);
}
h2.day{
  margin:0 0 10px;
  padding:8px 12px;
  font-size:13px;
  font-weight:600;
  letter-spacing:.18em;
  color:var(--green);
  background:linear-gradient(90deg, rgba(53,240,166,.12), transparent 72%);
  border-left:3px solid var(--green);
  border-radius:8px;
}
h3.session{
  margin:14px 0 2px;
  font-size:11px;
  font-weight:400;
  letter-spacing:.2em;
  color:var(--cyan);
  opacity:.9;
}
h3.session::before{content:"▸  "}
p.entry{
  margin:0;
  padding:2px 16px 2px 30px;
  border-left:1px solid transparent;
  white-space:pre-wrap;
  word-break:break-all;
  transition:background .12s,border-color .12s;
}
p.entry:hover{
  background:rgba(0,229,255,.05);
  border-left-color:var(--cyan);
}
.time{color:var(--dim);margin-right:10px}
.tag{color:var(--dim);margin-right:10px}
.module{color:var(--cyan)}
.pass .tag{color:var(--green)}
.pass .module{color:var(--green)}
.error .tag{color:var(--red)}
.error .module{color:var(--red)}
::selection{background:rgba(0,229,255,.25)}
@keyframes blink{50%{opacity:0}}
</style>
<script>
(function() {
  function rebuildLogView() {
    const body = document.body;
    if (!body) return;

    const header = body.querySelector('header');
    const existingNav = body.querySelector('.log-nav');
    if (existingNav) {
      existingNav.remove();
    }

    const dayNodes = Array.from(body.querySelectorAll('h2.day'));
    if (dayNodes.length === 0) {
      return;
    }

    const nav = document.createElement('nav');
    nav.className = 'log-nav';
    const label = document.createElement('span');
    label.className = 'log-nav-label';
    label.textContent = '快捷跳转';
    const list = document.createElement('div');
    list.className = 'log-nav-list';
    nav.appendChild(label);
    nav.appendChild(list);

    const cards = [];
    for (const heading of dayNodes) {
      const day = heading.getAttribute('data-day') || heading.textContent.trim().replace(/.*?(\d{4}-\d{2}-\d{2}).*/, '$1');
      const section = document.createElement('section');
      section.className = 'day-card';
      section.setAttribute('data-day', day);
      section.id = 'day-' + day;

      const headingClone = heading.cloneNode(true);
      headingClone.classList.add('day-header');
      section.appendChild(headingClone);

      let node = heading.nextSibling;
      while (node) {
        const next = node.nextSibling;
        const isNextDay = node.nodeType === 1 && node.tagName === 'H2' && node.classList.contains('day');
        if (isNextDay) {
          break;
        }
        section.appendChild(node);
        node = next;
      }
      cards.push({ day, section });

      const link = document.createElement('a');
      link.href = '#day-' + day;
      link.className = 'log-nav-item';
      link.textContent = day;
      list.appendChild(link);
    }

    const container = document.createElement('div');
    container.className = 'day-container';

    cards.sort(function(a, b) {
      return b.day.localeCompare(a.day);
    }).forEach(function(item) {
      container.appendChild(item.section);
    });

    dayNodes.forEach(function(node) {
      node.parentNode.removeChild(node);
    });

    const afterHeader = header ? header.nextSibling : null;
    if (afterHeader) {
      body.insertBefore(nav, afterHeader);
      body.insertBefore(container, afterHeader);
    } else {
      body.insertBefore(nav, body.firstChild);
      body.insertBefore(container, body.firstChild);
    }

    const temps = Array.from(body.querySelectorAll('h2.day'));
    temps.forEach(function(node) {
      node.parentNode.removeChild(node);
    });

    const sections = Array.from(body.querySelectorAll('.day-card'));
    sections.forEach(function(sectionNode) {
      const parent = sectionNode.parentNode;
      if (parent && parent !== container) {
        parent.removeChild(sectionNode);
      }
    });

    const existingContainer = body.querySelector('.day-container');
    if (existingContainer) {
      existingContainer.innerHTML = '';
      cards.sort(function(a, b) {
        return b.day.localeCompare(a.day);
      }).forEach(function(item) {
        existingContainer.appendChild(item.section);
      });
    }
  }

  if (document.readyState === 'loading') {
    window.addEventListener('DOMContentLoaded', rebuildLogView);
  } else {
    rebuildLogView();
  }
})();
</script>
</head>
<body class="log-body">
<header>
  <h1>系统日志</h1>
  <span class="cursor"></span>
  <span class="sub">后端框架 &middot; 实时监控</span>
</header>
)";
    }
}

void ShineLog::write(const std::string &moduleName, const std::string &message)
{
    writeLine(moduleName, message, Color::Black);
}

void ShineLog::pass(const std::string &moduleName, const std::string &message)
{
    writeLine(moduleName, message, Color::Green);
}

void ShineLog::error(const std::string &moduleName, const std::string &message)
{
    writeLine(moduleName, message, Color::Red);
}

void ShineLog::openLogFile(const std::string &day, const std::string &stamp)
{
    // 目录：LOG/<天>。建不出来（权限、只读盘之类）就退到当前目录，退不了就不写。
    std::error_code ec;
    const std::string preferredDirectory = logRootDirectory() + "/" + day;
    std::filesystem::create_directories(preferredDirectory, ec);
    std::string directory = ec ? (std::string(".") + "/" + kLogRootFolder + "/" + day) : preferredDirectory;
    if (ec)
    {
        std::error_code fallbackError;
        std::filesystem::create_directories(directory, fallbackError);
        if (fallbackError)
        {
            directory = ".";
        }
    }

    // 文件名用启动时刻；同一秒起了两个进程就顺延编号，不覆盖别人的日志。
    std::string name = stamp.substr(11); // "13:55:02" -> 文件名里不能有冒号
    for (char &ch : name)
    {
        if (ch == ':')
            ch = '-';
    }

    std::string path = directory + "/" + name + ".html";
    for (int suffix = 1; suffix < 1000 && std::filesystem::exists(path, ec); ++suffix)
    {
        path = directory + "/" + name + "-" + std::to_string(suffix) + ".html";
    }

    std::ofstream output(path, std::ios::out | std::ios::binary);
    if (!output)
    {
        // 这一份建不出来：留着空路径，下一行日志会再试一次
        _currentDay = day;
        _currentFile.clear();
        return;
    }

    // 一份文件 = 一天 × 一次运行，所以页面骨架、当天标题、会话标题在这里一次写完
    output << logPageHeader();
    output << "<h2 class=\"day\" data-day=\"" << day << "\">日期 // " << day << "</h2>\n";
    output << "<h3 class=\"session\">会话 // " << escapeHtml(stamp) << "</h3>\n";

    _currentDay = day;
    _currentFile = path;
}

void ShineLog::writeLine(const std::string &moduleName, const std::string &message, Color color)
{
    std::lock_guard<std::mutex> lock(_mutex);

    const std::string stamp = nowTime();
    const std::string day = stamp.substr(0, 10);
    // 第一次写日志，或者程序跨过零点跑到了新的一天：换一份新文件
    if (_currentFile.empty() || day != _currentDay)
    {
        openLogFile(day, stamp);
        if (_currentFile.empty())
        {
            return;
        }
    }

    std::ofstream output(_currentFile, std::ios::app | std::ios::binary);
    if (!output)
    {
        return;
    }

    const char *tag = color == Color::Green ? "通过" : (color == Color::Red ? "错误" : "日志");
    output
        << "<p class=\"entry " << colorName(color) << "\">"
        << "<span class=\"time\">"
        << "[" << escapeHtml(stamp) << "]"
        << "</span>"
        << "<span class=\"tag\">[" << tag << "]</span>"
        << "<span class=\"module\">[" << escapeHtml(moduleName) << "]</span> "
        << escapeHtml(message)
        << "</p>\n";
}

std::string ShineLog::colorName(Color color)
{
    switch (color)
    {
    case Color::Green:
        return "pass";
    case Color::Red:
        return "error";
    case Color::Black:
        return "info";
    }

    return "info";
}

std::string ShineLog::nowTime()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

std::string ShineLog::logRootDirectory()
{
    std::string directory;
#ifdef _WIN32
    std::vector<char> buffer(MAX_PATH);
    const DWORD size = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0)
    {
        directory = directoryOf(std::string(buffer.data(), size));
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0)
    {
        directory = directoryOf(buffer.data());
    }
#else
    std::vector<char> buffer(4096);
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size > 0)
    {
        buffer[static_cast<std::size_t>(size)] = '\0';
        directory = directoryOf(buffer.data());
    }
#endif

    // 取不到可执行文件路径时退到当前目录，至少日志不会丢
    if (directory.empty())
    {
        directory = ".";
    }

    return directory + "/" + kLogRootFolder;
}

std::string ShineLog::escapeHtml(const std::string &text)
{
    std::string escaped;
    escaped.reserve(text.size());

    for (const char item : text)
    {
        switch (item)
        {
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
