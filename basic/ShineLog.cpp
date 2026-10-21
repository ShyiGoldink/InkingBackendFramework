#include "ShineLog.h"

#include "ShineBasicModule.h"
#include "ShineStatusChecker.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
    /** 内存环形缓冲容量：`log tail/grep` 直接读这里，不碰磁盘。 */
    constexpr std::size_t kRingCapacity = 2000;

    /** 同一个键每秒最多落盘多少条；超出的只计数，由 FOLD 行汇总。 */
    constexpr int kMaxRecordsPerSecondPerKey = 5;

    /** 命令回显保留的最大字符数，防止一条超长 SQL 把日志撑爆。 */
    constexpr std::size_t kCommandPreviewLimit = 200;

    /** 日志根目录：可执行文件同级的 LOG/。 */
    constexpr const char *kLogRootFolder = "LOG";

    // ---- 时间与路径工具 ----

    std::tm localTimeOf(const std::time_t value)
    {
        std::tm parts{};
#ifdef _WIN32
        localtime_s(&parts, &value);
#else
        localtime_r(&value, &parts);
#endif
        return parts;
    }

    std::string formatTime(const std::chrono::system_clock::time_point &point, const char *format)
    {
        const std::time_t value = std::chrono::system_clock::to_time_t(point);
        const std::tm parts = localTimeOf(value);
        std::ostringstream output;
        output << std::put_time(&parts, format);
        return output.str();
    }

    long long millisecondsOf(const std::chrono::system_clock::time_point &point)
    {
        return static_cast<long long>(
                   std::chrono::duration_cast<std::chrono::milliseconds>(point.time_since_epoch()).count()) %
               1000;
    }

    std::string timeOfDay(const std::chrono::system_clock::time_point &point)
    {
        std::ostringstream output;
        output << formatTime(point, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
               << millisecondsOf(point);
        return output.str();
    }

    std::string dateOf(const std::chrono::system_clock::time_point &point)
    {
        return formatTime(point, "%Y-%m-%d");
    }

    std::string fullStamp(const std::chrono::system_clock::time_point &point)
    {
        std::ostringstream output;
        output << formatTime(point, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
               << millisecondsOf(point);
        return output.str();
    }

    std::string directoryOf(const std::string &filePath)
    {
        const std::size_t lastSlash = filePath.find_last_of("/\\");
        if (lastSlash == std::string::npos)
        {
            return ".";
        }
        return filePath.substr(0, lastSlash);
    }

    std::string fileNameOf(const std::string &filePath)
    {
        const std::size_t lastSlash = filePath.find_last_of("/\\");
        if (lastSlash == std::string::npos)
        {
            return filePath;
        }
        return filePath.substr(lastSlash + 1);
    }

    long processId()
    {
#ifdef _WIN32
        return static_cast<long>(_getpid());
#else
        return static_cast<long>(getpid());
#endif
    }

    /** 可执行文件所在目录；取不到时退回当前目录。 */
    std::string executableDirectory()
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
        if (directory.empty())
        {
            directory = ".";
        }
        return directory;
    }

    /** 线程短号：按创建顺序编号，显示为 T-01、T-02，比裸 id 好读也稳定。 */
    int shortThreadId()
    {
        thread_local const int assigned = []()
        {
            static std::atomic<int> next{1};
            return next.fetch_add(1);
        }();
        return assigned;
    }

    const char *levelTag(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warn:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        }
        return "INFO ";
    }

    /** 伪级别：命令回显、人工打点、快照、折叠汇总。 */
    constexpr const char *kTagCommand = "CMD  ";
    constexpr const char *kTagMark = "MARK ";
    constexpr const char *kTagSnapshot = "SNAP ";
    constexpr const char *kTagFold = "FOLD ";

    std::string twoDigits(int value)
    {
        std::ostringstream output;
        output << std::setw(2) << std::setfill('0') << value;
        return output.str();
    }

    std::string sixDigits(std::uint64_t value)
    {
        std::ostringstream output;
        output << std::setw(6) << std::setfill('0') << value;
        return output.str();
    }

    std::string truncate(const std::string &text, std::size_t limit)
    {
        if (text.size() <= limit)
        {
            return text;
        }
        // 按字节截断可能切坏一个多字节字符，往回退到 ASCII 边界为止。
        std::size_t cut = limit;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        {
            --cut;
        }
        return text.substr(0, cut) + "…(已截断)";
    }

    /** 把多行内容压成单行：日志的行结构不能被消息里的换行破坏。 */
    std::string flatten(const std::string &text)
    {
        std::string result;
        result.reserve(text.size());
        for (const char item : text)
        {
            if (item == '\r')
            {
                continue;
            }
            result += (item == '\n') ? ' ' : item;
        }
        return result;
    }

    struct SnapshotRow
    {
        std::string module;
        int step = -1;
        std::string signature; /** 只含"状态与消息"，用于判断是否变化 */
        std::string text;      /** 展示用完整描述，含次数与时间 */
    };

    struct Impl
    {
        std::mutex mutex;
        std::ofstream file;
        std::string path;
        std::string runId;
        std::string currentDay;
        LogLevel threshold = kDefaultLogLevel;
        bool initialized = false;
        bool closed = false;
        std::uint64_t sequence = 0;
        std::uint64_t records = 0;
        std::uint64_t folded = 0;
        std::uint64_t suppressed = 0;

        std::deque<std::string> ring;

        // 折叠状态：只在"连续相同"时生效，被别的记录打断就结算
        std::string pendingKey;
        std::string pendingModule;
        int pendingStep = -1;
        std::uint64_t pendingCount = 0;
        std::uint64_t pendingSuppressed = 0;
        std::string pendingFirst;
        std::string pendingLast;

        // 限流窗口
        long long rateSecond = -1;
        std::unordered_map<std::string, int> rate;

        // 统计
        std::unordered_map<std::string, std::uint64_t> levelCounts;
        std::unordered_map<std::string, std::uint64_t> moduleCounts;

        // 快照
        bool snapshotEnabled = true;
        std::chrono::seconds snapshotInterval{300};
        std::unordered_map<std::string, std::string> lastSnapshot;
        std::thread snapshotThread;
        std::mutex snapshotMutex;
        std::condition_variable snapshotCv;
        bool snapshotStop = false;

        // 控制台镜像（默认关闭）
        ShineLog::MirrorSink mirror;
        bool mirrorEnabled = false;
        LogLevel mirrorMinLevel = LogLevel::Error;
    };

    /**
     * 日志状态故意不析构。
     *
     * 静态对象析构顺序不可控：模块的静态析构函数里若还有一次日志调用，
     * 撞上"日志对象已经析构"就是 use-after-free。这里放一份永不释放的状态，
     * 由 shutdown() 负责把文件正常收尾，代价只有几十字节。
     */
    Impl &state()
    {
        static Impl *instance = new Impl();
        return *instance;
    }

    void pushRing(Impl &st, const std::string &line)
    {
        st.ring.push_back(line);
        if (st.ring.size() > kRingCapacity)
        {
            st.ring.pop_front();
        }
    }

    void writeLineLocked(Impl &st, const std::string &line)
    {
        pushRing(st, line);
        if (st.file)
        {
            st.file << line << '\n';
            st.file.flush();
        }
        ++st.records;
    }

    std::string composeLine(const char *tag, const std::string &module, int step, std::uint64_t sequence,
                            const std::string &site, const std::string &message)
    {
        std::ostringstream output;
        output << timeOfDay(std::chrono::system_clock::now()) << " | " << tag << " | " << module << ':'
               << (step >= 0 ? twoDigits(step) : std::string("--")) << " | #" << sixDigits(sequence)
               << " | T-" << twoDigits(shortThreadId()) << " | " << site << " | " << message;
        return output.str();
    }

    /** 结算上一段折叠：只有真的重复过才补一行 FOLD，避免多写一行废话。 */
    void flushPendingLocked(Impl &st)
    {
        if (st.pendingCount > 1 || st.pendingSuppressed > 0)
        {
            st.folded += st.pendingCount > 1 ? (st.pendingCount - 1) : 0;
            std::ostringstream message;
            message << "上一条消息重复 ×" << st.pendingCount;
            if (st.pendingSuppressed > 0)
            {
                message << "，另有 " << st.pendingSuppressed << " 条被限流丢弃";
            }
            message << "，" << st.pendingFirst << " ~ " << st.pendingLast;
            const std::string line =
                composeLine(kTagFold, st.pendingModule, st.pendingStep, ++st.sequence, "ShineLog.cpp:0",
                            message.str());
            writeLineLocked(st, line);
        }
        st.pendingKey.clear();
        st.pendingCount = 0;
        st.pendingSuppressed = 0;
    }

    bool openFileLocked(Impl &st, const std::chrono::system_clock::time_point &now)
    {
        const std::string day = dateOf(now);
        const std::string root = executableDirectory() + "/" + kLogRootFolder;

        std::error_code error;
        const std::string directory = root + "/" + day;
        std::filesystem::create_directories(directory, error);
        if (error)
        {
            // 目录建不出来（只读盘、权限）就退到当前工作目录，至少不丢日志
            std::error_code fallbackError;
            std::filesystem::create_directories(std::string(kLogRootFolder) + "/" + day, fallbackError);
            if (fallbackError)
            {
                return false;
            }
        }

        const std::string base =
            "run-" + formatTime(now, "%H%M%S") + "-" + std::to_string(processId());
        std::string path = directory + "/" + base + ".log";
        for (int suffix = 1; suffix < 1000; ++suffix)
        {
            if (!std::filesystem::exists(path, error))
            {
                break;
            }
            path = directory + "/" + base + "-" + std::to_string(suffix) + ".log";
        }

        st.file.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!st.file)
        {
            return false;
        }
        st.file.setf(std::ios::unitbuf);
        st.path = path;
        st.currentDay = day;
        st.runId = "r-" + formatTime(now, "%Y%m%d-%H%M%S") + "-" + std::to_string(processId());

        std::error_code cwdError;
        const std::string cwd = std::filesystem::current_path(cwdError).string();

        std::ostringstream header;
        header << "# InkingBackendFramework log\n"
               << "# run        " << st.runId << '\n'
               << "# started    " << fullStamp(now) << '\n'
               << "# exedir     " << executableDirectory() << '\n'
               << "# cwd        " << (cwdError ? std::string("?") : cwd) << '\n'
               << "# level      " << levelTag(st.threshold) << '\n'
               << "# snapshot   " << (st.snapshotEnabled ? std::to_string(st.snapshotInterval.count()) + "s" : "off")
               << '\n';
        st.file << header.str();
        st.file.flush();

        // latest.txt：给"最近一次运行在哪"一个稳定入口，不依赖符号链接权限
        std::ofstream latest(root + "/latest.txt", std::ios::out | std::ios::binary | std::ios::trunc);
        if (latest)
        {
            latest << "run=" << st.runId << '\n'
                   << "path=" << day << "/" << fileNameOf(path) << '\n'
                   << "started=" << fullStamp(now) << '\n'
                   << "pid=" << processId() << '\n';
        }
        return true;
    }

    void rollDayLocked(Impl &st, const std::chrono::system_clock::time_point &now)
    {
        const std::string day = dateOf(now);
        flushPendingLocked(st);
        std::ostringstream marker;
        marker << "# ---- 跨日 " << day << "，本次运行继续写入同一文件 ----";
        writeLineLocked(st, marker.str());
        st.currentDay = day;
    }

    /** 记录一条（可按级别过滤、可折叠）。返回需要镜像到控制台的行，空串表示不镜像。 */
    std::string emit(LogLevel level, const char *tag, const std::string &module, int step,
                     const std::string &message, const std::string &site, bool foldable, bool force)
    {
        Impl &st = state();
        ShineLog::MirrorSink mirror;
        std::string mirrored;

        {
            std::lock_guard<std::mutex> lock(st.mutex);
            if (!st.initialized || st.closed)
            {
                return {};
            }
            // CMD / MARK / SNAP 是定位锚点，不受阈值影响：把级别调高也不该丢掉"我执行了什么"
            if (!force && level < st.threshold)
            {
                return {};
            }

            const auto now = std::chrono::system_clock::now();
            const std::string flat = flatten(message);

            const long long second =
                std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            if (second != st.rateSecond)
            {
                st.rateSecond = second;
                st.rate.clear();
            }

            const std::string key = std::string(levelTag(level)) + '|' + module + '|' +
                                    std::to_string(step) + '|' + std::to_string(std::hash<std::string>{}(flat));

            // 折叠：连续相同就不再落盘，只在结算时补一行 FOLD
            if (foldable && !st.pendingKey.empty() && key == st.pendingKey)
            {
                ++st.pendingCount;
                st.pendingLast = timeOfDay(now);
                return {};
            }

            // 限流：同一个键每秒超过上限就只计数
            if (foldable)
            {
                int &bucket = st.rate[key];
                if (bucket >= kMaxRecordsPerSecondPerKey)
                {
                    ++st.suppressed;
                    if (key == st.pendingKey)
                    {
                        ++st.pendingSuppressed;
                    }
                    return {};
                }
                ++bucket;
            }

            // 走到这里一定换了键（相同键在折叠分支已经返回），先把上一段结算掉
            flushPendingLocked(st);

            if (st.currentDay != dateOf(now))
            {
                rollDayLocked(st, now);
            }

            const std::string line = composeLine(tag, module, step, ++st.sequence, site, flat);
            writeLineLocked(st, line);

            ++st.levelCounts[levelTag(level)];
            ++st.moduleCounts[module];

            if (foldable)
            {
                st.pendingKey = key;
                st.pendingModule = module;
                st.pendingStep = step;
                st.pendingCount = 1;
                st.pendingSuppressed = 0;
                st.pendingFirst = timeOfDay(now);
                st.pendingLast = st.pendingFirst;
            }

            if (st.mirrorEnabled && level >= st.mirrorMinLevel && st.mirror)
            {
                mirror = st.mirror;
                mirrored = line;
            }
        }

        if (mirror && !mirrored.empty())
        {
            // 回调放在锁外：镜像出口可能走消息队列，不该持着日志锁做别的事
            mirror(level, mirrored);
        }
        return mirrored;
    }
}

// ---- 公共接口 ----

void ShineLog::init(const LogOptions &options)
{
    Impl &st = state();
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        if (st.initialized)
        {
            return;
        }
        st.threshold = options.level;
        st.snapshotEnabled = options.snapshotEnabled;
        st.snapshotInterval = options.snapshotInterval;

        if (const char *levelText = std::getenv("INKING_LOG_LEVEL"))
        {
            LogLevel parsed = st.threshold;
            if (parseLevel(levelText, parsed))
            {
                st.threshold = parsed;
            }
        }
        if (const char *snapshotText = std::getenv("INKING_LOG_SNAPSHOT"))
        {
            const std::string text = snapshotText;
            st.snapshotEnabled = !(text == "0" || text == "off" || text == "false");
        }
        if (const char *intervalText = std::getenv("INKING_LOG_SNAPSHOT_INTERVAL"))
        {
            // 排障时可以把间隔调短，观察"稳态零写入"的差分行为
            try
            {
                const long long seconds = std::stoll(intervalText);
                if (seconds > 0)
                {
                    st.snapshotInterval = std::chrono::seconds(seconds);
                }
            }
            catch (const std::exception &)
            {
                // 配错了就忽略，保持默认节奏
            }
        }
        if (const char *consoleText = std::getenv("INKING_LOG_CONSOLE"))
        {
            LogLevel parsed = LogLevel::Error;
            if (parseLevel(consoleText, parsed))
            {
                st.mirrorEnabled = true;
                st.mirrorMinLevel = parsed;
            }
        }

        const auto now = std::chrono::system_clock::now();
        if (!openFileLocked(st, now))
        {
            return;
        }
        st.initialized = true;
    }

    std::atexit([]()
                { ShineLog::shutdown(); });

    if (st.snapshotEnabled && st.snapshotInterval.count() > 0)
    {
        Impl *context = &st;
        const auto interval = st.snapshotInterval;
        context->snapshotThread = std::thread([context, interval]()
                                              {
                                                  std::unique_lock<std::mutex> lock(context->snapshotMutex);
                                                  while (!context->snapshotStop)
                                                  {
                                                      if (context->snapshotCv.wait_for(
                                                              lock, interval,
                                                              [context]()
                                                              { return context->snapshotStop; }))
                                                      {
                                                          break;
                                                      }
                                                      lock.unlock();
                                                      ShineLog::snapshot("periodic");
                                                      lock.lock();
                                                  }
                                              });
    }
}

void ShineLog::shutdown()
{
    Impl &st = state();
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        // 这里只做"是否已经开始收尾"的判断；真正置 closed 要放在退出快照之后，
        // 否则 snapshot() 会以为日志已经关闭，直接把最后一份快照吞掉。
        if (!st.initialized || st.closed)
        {
            return;
        }
    }

    // 先停快照线程，避免 shutdown 与 periodic 抢同一把锁
    {
        std::lock_guard<std::mutex> guard(st.snapshotMutex);
        st.snapshotStop = true;
    }
    st.snapshotCv.notify_all();
    if (st.snapshotThread.joinable())
    {
        st.snapshotThread.join();
    }

    snapshot("exit");

    std::lock_guard<std::mutex> lock(st.mutex);
    if (st.closed)
    {
        return;
    }
    st.closed = true;
    flushPendingLocked(st);

    if (st.file)
    {
        std::ostringstream footer;
        footer << "# ended      " << fullStamp(std::chrono::system_clock::now()) << '\n'
               << "# records    " << st.records << " (折叠 " << st.folded << "，限流 " << st.suppressed
               << ")\n";
        st.file << footer.str();
        st.file.flush();
        st.file.close();
    }
}

void ShineLog::setMirrorSink(MirrorSink sink)
{
    Impl &st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.mirror = std::move(sink);
}

void ShineLog::setLevel(LogLevel level)
{
    Impl &st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.threshold = level;
}

LogLevel ShineLog::level()
{
    Impl &st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    return st.threshold;
}

bool ShineLog::parseLevel(std::string_view text, LogLevel &result)
{
    std::string lower;
    lower.reserve(text.size());
    for (const char item : text)
    {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(item)));
    }

    if (lower == "trace")
    {
        result = LogLevel::Trace;
    }
    else if (lower == "debug")
    {
        result = LogLevel::Debug;
    }
    else if (lower == "info")
    {
        result = LogLevel::Info;
    }
    else if (lower == "warn" || lower == "warning")
    {
        result = LogLevel::Warn;
    }
    else if (lower == "error")
    {
        result = LogLevel::Error;
    }
    else if (lower == "fatal")
    {
        result = LogLevel::Fatal;
    }
    else
    {
        return false;
    }
    return true;
}

void ShineLog::log(LogLevel level, std::string_view module, int stage, std::string_view message,
                   const std::source_location &site)
{
    if (message.empty())
    {
        return;
    }
    // Error 及以上不参与折叠：失败证据必须逐条保留
    const bool foldable = level < LogLevel::Error;
    const std::string location = std::string(fileNameOf(site.file_name())) + ':' +
                                 std::to_string(site.line());
    emit(level, levelTag(level), std::string(module), stage, std::string(message), location, foldable, false);
}

void ShineLog::command(std::string_view line, const std::source_location &site)
{
    const std::string location = std::string(fileNameOf(site.file_name())) + ':' +
                                 std::to_string(site.line());
    emit(LogLevel::Info, kTagCommand, "UIThread", -1, "> " + truncate(std::string(line), kCommandPreviewLimit),
         location, false, true);
}

void ShineLog::mark(std::string_view text, const std::source_location &site)
{
    const std::string location = std::string(fileNameOf(site.file_name())) + ':' +
                                 std::to_string(site.line());
    emit(LogLevel::Info, kTagMark, "ShineLog", -1, std::string(text), location, false, true);
}

void ShineLog::snapshot(std::string_view reason)
{
    Impl &st = state();
    if (!st.initialized || st.closed)
    {
        return;
    }

    // 先在锁外把状态表拷出来：模块手里的 _stageMutex 与日志锁是两把锁，
    // 反过来（先日志锁再阶段锁）就会和 setStageStatus 形成 ABBA 死锁。
    std::vector<SnapshotRow> rows;
    for (const auto &name : ShineStatusChecker::getAllModuleNames())
    {
        for (ShineBasicModule *module : ShineStatusChecker::getModules(name))
        {
            if (module == nullptr)
            {
                continue;
            }
            for (const Stage &stage : module->getStage())
            {
                if (stage.name.empty())
                {
                    continue; // 只登记过详情、函数还没跑到的阶段不参与快照
                }
                SnapshotRow row;
                row.module = module->moduleName();
                row.step = stage.step;
                row.signature = stage.name + '|' + (stage.status ? "1" : "0") + '|' + stage.message;

                std::ostringstream text;
                text << stage.name << ' ' << (stage.status ? "通过" : "失败") << " ×" << stage.count;
                // 首次与最近相同时只写一个时间，避免短命进程的快照里出现两遍同样的时刻
                if (!stage.firstSeen.empty() && stage.firstSeen != stage.lastSeen)
                {
                    text << " 首次 " << stage.firstSeen << " 最近 " << stage.lastSeen;
                }
                else if (!stage.lastSeen.empty())
                {
                    text << " 最近 " << stage.lastSeen;
                }
                if (!stage.message.empty())
                {
                    text << " - " << stage.message;
                }
                if (!stage.status && !stage.suggestion.empty())
                {
                    text << " 建议: " << stage.suggestion;
                }
                row.text = text.str();
                rows.push_back(std::move(row));
            }
        }
    }

    const bool full = (reason != "periodic");

    std::lock_guard<std::mutex> lock(st.mutex);
    if (!st.initialized || st.closed)
    {
        return;
    }

    std::vector<const SnapshotRow *> changed;
    std::vector<const SnapshotRow *> all;
    all.reserve(rows.size());
    for (const SnapshotRow &row : rows)
    {
        all.push_back(&row);
        const std::string key = row.module + ':' + std::to_string(row.step);
        const auto existing = st.lastSnapshot.find(key);
        if (existing == st.lastSnapshot.end() || existing->second != row.signature)
        {
            changed.push_back(&row);
            st.lastSnapshot[key] = row.signature;
        }
    }

    // 差分快照：没有任何变化就一个字节都不写，稳态开销为零
    if (!full && changed.empty())
    {
        return;
    }

    // 退出/手动要的是"终态全貌"，周期性快照只要"这一轮的增量"
    const std::vector<const SnapshotRow *> &toWrite = full ? all : changed;

    flushPendingLocked(st);

    const std::string reasonText(reason);
    {
        std::ostringstream begin;
        begin << "===== 状态快照 " << reasonText << " 起 =====";
        writeLineLocked(st, composeLine(kTagSnapshot, "ShineLog", -1, ++st.sequence, "ShineLog.cpp:0",
                                        begin.str()));
    }

    for (const SnapshotRow *row : toWrite)
    {
        // 快照同样必须是"一行一个阶段"：阶段说明和建议里可能带换行，
        // 直接写进去会把日志的行结构撕开（旧版就是被多行建议撑坏的）。
        writeLineLocked(st, composeLine(kTagSnapshot, row->module, row->step, ++st.sequence, "ShineLog.cpp:0",
                                        flatten(row->text)));
    }

    {
        std::ostringstream end;
        end << "===== 状态快照 止：共 " << rows.size() << " 个阶段，本次记录 " << toWrite.size()
            << " 项 =====";
        writeLineLocked(st, composeLine(kTagSnapshot, "ShineLog", -1, ++st.sequence, "ShineLog.cpp:0", end.str()));
    }
}

std::string ShineLog::filePath()
{
    Impl &st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    return st.path;
}

std::vector<std::string> ShineLog::tail(std::size_t count)
{
    Impl &st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    flushPendingLocked(st);

    std::vector<std::string> result;
    const std::size_t begin = st.ring.size() > count ? st.ring.size() - count : 0;
    result.reserve(st.ring.size() - begin);
    for (std::size_t index = begin; index < st.ring.size(); ++index)
    {
        result.push_back(st.ring[index]);
    }
    return result;
}

std::vector<std::string> ShineLog::grep(std::string_view pattern, std::string_view module)
{
    Impl &st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    flushPendingLocked(st);

    std::vector<std::string> result;
    const std::string needle(pattern);
    const std::string moduleNeedle = module.empty() ? std::string() : ("| " + std::string(module) + ':');
    for (const std::string &line : st.ring)
    {
        if (!moduleNeedle.empty() && line.find(moduleNeedle) == std::string::npos)
        {
            continue;
        }
        if (!needle.empty() && line.find(needle) == std::string::npos)
        {
            continue;
        }
        result.push_back(line);
    }
    return result;
}

std::string ShineLog::statistics()
{
    Impl &st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    flushPendingLocked(st);

    std::ostringstream output;
    output << "日志统计" << '\n';
    output << "  文件      " << (st.path.empty() ? "(未初始化)" : st.path) << '\n';
    output << "  运行标识  " << (st.runId.empty() ? "-" : st.runId) << '\n'
           << "  阈值      " << levelTag(st.threshold) << '\n'
           << "  落盘条数  " << st.records << '\n'
           << "  折叠条数  " << st.folded << '\n'
           << "  限流丢弃  " << st.suppressed << '\n';

    output << "  按级别    ";
    bool firstLevel = true;
    for (const char *tag : {"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"})
    {
        const auto iter = st.levelCounts.find(tag);
        if (iter == st.levelCounts.end())
        {
            continue;
        }
        if (!firstLevel)
        {
            output << " / ";
        }
        output << tag << ' ' << iter->second;
        firstLevel = false;
    }
    if (firstLevel)
    {
        output << "无";
    }
    output << '\n';

    output << "  按模块    ";
    bool firstModule = true;
    for (const auto &entry : st.moduleCounts)
    {
        if (!firstModule)
        {
            output << " / ";
        }
        output << entry.first << ' ' << entry.second;
        firstModule = false;
    }
    if (firstModule)
    {
        output << "无";
    }
    return output.str();
}

std::string ShineLog::timestamp()
{
    const auto now = std::chrono::system_clock::now();
    return formatTime(now, "%H:%M:%S");
}

// ---- 兼容层 ----

void ShineLog::write(const std::string &moduleName, const std::string &message)
{
    log(LogLevel::Info, moduleName, -1, message);
}

void ShineLog::pass(const std::string &moduleName, const std::string &message)
{
    log(LogLevel::Info, moduleName, -1, message);
}

void ShineLog::error(const std::string &moduleName, const std::string &message)
{
    log(LogLevel::Error, moduleName, -1, message);
}
