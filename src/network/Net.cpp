#include "network/Net.h"

#include "basic/ShineLog.h"
#include "business/NetworkCommandRegistrant.h"
#include "network/Listener.h"
#include "network/NetworkConfig.h"
#include "network/Shard.h"
#include "ui/UIMessageLibrary.h"

#include <memory>
#include <vector>

Net::Net()
{
    registerToStatusChecker();
}

Net::~Net()
{
    // 正常路径下 socket 已经在 run() 的收尾流程里关掉了，这里只兜底
    closeListenSocket();
    SocketOps::cleanUp();
}

void Net::run()
{
    // 每次进入都从「未请求退出」开始：run() 可以被重复调用（network-run 命令），
    // 上一次退出留下的标志不能让这一次刚起来就立刻结束。
    _stopRequested.store(false);

    if (_acceptSocket != kInvalidSocket)
    {
        setStageStatus(NetworkStage::kStartUp, "启动！", false,
                       "网络服务已经启动，不能重复运行");
        return;
    }

    std::string configNote;
    const NetworkConfig config = loadNetworkConfig(configNote);
    ShineLog::log(LogLevel::Info, kNetModuleName, NetworkStage::kStartUp,
                  "网络配置：" + configNote);

    startUp(config.port);
    if (_acceptSocket == kInvalidSocket)
    {
        return;
    }

    // ① 先注册线上指令，再起分片：注册表在分片第一次查表之前就必须是冻结状态，
    // 否则就成了「运行期改一张正在被并发读的表」。
    registerBuiltinNetworkCommands();

    // ② 分片起来：接入线程一开跑就会立刻把新连接交给分片，目标必须已经存在
    std::vector<std::unique_ptr<Shard>> shards;
    shards.reserve(config.shardCount);
    std::string errorMessage;

    for (std::size_t index = 0; index < config.shardCount; ++index)
    {
        auto shard = std::make_unique<Shard>(index, config.maxConnectionsPerShard);
        if (!shard->start(errorMessage))
        {
            setStageStatus(NetworkStage::kShard, "启动分片", false,
                           "分片 " + std::to_string(index) + " 启动失败：" + errorMessage);
            UIMessageLibrary::addMessage(
                MessageType::error, 0.0f,
                "分片 " + std::to_string(index) + " 启动失败，网络服务无法启动");
            // shards 里已经起来的那些由各自的析构停掉并 join，不留半启动状态
            closeListenSocket();
            return;
        }
        shards.push_back(std::move(shard));
    }

    setStageStatus(NetworkStage::kShard, "启动分片", true,
                   std::to_string(shards.size()) + " 个分片已启动");

    std::vector<Shard *> shardRefs;
    shardRefs.reserve(shards.size());
    for (const auto &shard : shards)
    {
        shardRefs.push_back(shard.get());
    }

    // ② 再起接入线程。它借用监听 socket，不负责关闭
    Listener listener(_acceptSocket, shardRefs, config.maxConnectionsPerShard);
    if (!listener.start(errorMessage))
    {
        setStageStatus(NetworkStage::kAcceptor, "启动接入线程", false, errorMessage);
        UIMessageLibrary::addMessage(
            MessageType::error, 0.0f, "接入线程启动失败，网络服务无法启动");
        for (const auto &shard : shards)
        {
            shard->requestStop();
        }
        for (const auto &shard : shards)
        {
            shard->join();
        }
        closeListenSocket();
        return;
    }

    setStageStatus(NetworkStage::kAcceptor, "启动接入线程", true,
                   std::to_string(shards.size()) + " 个分片参与分流");

    UIMessageLibrary::addMessage(
        MessageType::normal, 0.0f,
        "网络服务已启动：" + config.describe());

    // ③ 睡到被要求停止。条件变量而不是轮询睡眠：stop() 一到就醒，退出不靠时间片
    {
        std::unique_lock<std::mutex> lock(_stopMutex);
        _stopCondition.wait(lock, [this]()
                            { return _stopRequested.load(); });
    }

    // ④ 停机顺序不能变：先断接入（不再有新连接进来），再停分片（它们只处理存量），
    //    最后关监听。反过来做的话，accept 可能刚刚接过一条连接、又往已停的分片里塞。
    listener.requestStop();
    listener.join();
    setStageStatus(NetworkStage::kStopAcceptor, "停止接入线程", true,
                   "累计接受连接 " + std::to_string(listener.acceptedCount()));

    for (const auto &shard : shards)
    {
        shard->requestStop();
    }
    for (const auto &shard : shards)
    {
        shard->join();
    }

    closeListenSocket();

    setStageStatus(NetworkStage::kShutdown, "停止网络服务", true,
                   std::to_string(shards.size()) + " 个分片与接入线程均已退出");
}

void Net::stop()
{
    _stopRequested.store(true);
    _stopCondition.notify_all();
}

void Net::startUp(const int &port)
{
    if (_acceptSocket != kInvalidSocket)
    {
        setStageStatus(NetworkStage::kStartUp, "启动！", false,
                       "网络服务已经启动，不能重复创建监听Socket");
        return;
    }

    bool success = false;
    std::string errorMessage;
    if (applyNetworkServer() && createAcceptSocket() && stepBind(port) && stepListen())
    {
        success = true;
    }
    else
    {
        errorMessage = "请检查其它阶段报错";
    }

    setStageStatus(NetworkStage::kStartUp, "启动！", success, errorMessage);
}

void Net::closeListenSocket()
{
    if (_acceptSocket == kInvalidSocket)
    {
        return;
    }
    SocketOps::close(_acceptSocket);
    _acceptSocket = kInvalidSocket;
}

bool Net::applyNetworkServer()
{
    std::string errorMessage;
    const bool success = SocketOps::startUp(errorMessage);

    // 这四个是「启动」流水线的内部环节：成功时不单独刷日志，由 startUp() 汇总；
    // 失败时静默策略自动放行 Error，出问题依然能一眼看出挂在哪一步。
    setStageStatus(NetworkStage::kApplyNetworkServer, "申请本地的网络服务请求", success,
                   errorMessage, StageLogPolicy::Silent);

    return success;
}

bool Net::createAcceptSocket()
{
    bool success = false;
    std::string errorMessage;

    _acceptSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_acceptSocket == kInvalidSocket)
    {
        errorMessage = "Socket创建请求失败:" + SocketOps::lastErrorText();
        setStageStatus(NetworkStage::kCreateSocket, "创建接受连接请求专用的Socket", false,
                       errorMessage, StageLogPolicy::Silent);
        return false;
    }

    if (!SocketOps::setReuseAddress(_acceptSocket))
    {
        errorMessage = "设置SO_REUSEADDR失败:" + SocketOps::lastErrorText();
        closeListenSocket();
        setStageStatus(NetworkStage::kCreateSocket, "创建接受连接请求专用的Socket", false,
                       errorMessage, StageLogPolicy::Silent);
        return false;
    }

    // 顺手就设成非阻塞：accept 循环要「接到 EAGAIN 为止」，
    // 少了这一步就会卡在 accept 上，后面 1000 条连接一条都接不进来。
    if (!SocketOps::setNonBlocking(_acceptSocket))
    {
        errorMessage = "把监听Socket设为非阻塞失败:" + SocketOps::lastErrorText();
        closeListenSocket();
        setStageStatus(NetworkStage::kCreateSocket, "创建接受连接请求专用的Socket", false,
                       errorMessage, StageLogPolicy::Silent);
        return false;
    }

    success = true;
    errorMessage.clear();
    setStageStatus(NetworkStage::kCreateSocket, "创建接受连接请求专用的Socket", success,
                   errorMessage, StageLogPolicy::Silent);
    return success;
}

bool Net::stepBind(const int &port)
{
    bool success = false;
    std::string errorMessage;

    sockaddr_in serverAddress = {};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(static_cast<unsigned short>(port));

    if (::bind(_acceptSocket, reinterpret_cast<sockaddr *>(&serverAddress),
               sizeof(serverAddress)) == SOCKET_ERROR)
    {
        errorMessage = "Bind[绑定]请求失败:" + SocketOps::lastErrorText();
        closeListenSocket();
    }
    else
    {
        success = true;
    }

    setStageStatus(NetworkStage::kBind, "绑定端口", success, errorMessage,
                   StageLogPolicy::Silent);
    return success;
}

bool Net::stepListen()
{
    bool success = false;
    std::string errorMessage;

    // backlog 从 128 提到 1024：压测时会一次性涌进 1000 条连接，
    // backlog 太小会有一部分连不上（现象是「1000 条里只有几百条连上」）。
    if (::listen(_acceptSocket, 1024) == SOCKET_ERROR)
    {
        errorMessage = "Listen[监听]请求失败:" + SocketOps::lastErrorText();
        closeListenSocket();
    }
    else
    {
        success = true;
    }

    setStageStatus(NetworkStage::kListen, "监听端口", success, errorMessage,
                   StageLogPolicy::Silent);
    return success;
}
