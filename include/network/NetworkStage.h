#ifndef INKING_BACKEND_FRAMEWORK_NETWORK_STAGE_H
#define INKING_BACKEND_FRAMEWORK_NETWORK_STAGE_H

/**
 * @file NetworkStage.h
 * @brief 网络模块的公共常量：模块名 + 自检阶段编号。
 *
 * 单独抽一个头文件，是因为这些常量要同时被四处用到：Net（门面）、Shard（分片线程）、
 * Listener（接入线程）、指令库（按模块名过滤 status 输出）。日志里的阶段编号是定位锚点，
 * 各处各写一套数字迟早会撞车，撞车之后 `status` 里就会出现「名称不一致」的告警，
 * 那时候再来找是哪两处对不上很麻烦。
 *
 * 编号只增不改：已经出现在历史日志里的编号必须永远指向同一件事，否则日志就没法回溯。
 */

/** 模块名：自检注册、日志的「模块:阶段」段都用它 */
inline constexpr const char *kNetModuleName = "Net";

namespace NetworkStage
{
    /*
     * 一个编号只能对应一个名字。
     *
     * 自检的阶段表按编号索引，而且**拒绝同名替换**：同一个编号第二次以不同名称登记，
     * 会直接报「阶段编号冲突」并丢掉这次更新。所以「启动接入线程」和「停止接入线程」
     * 必须是两个编号——省一个编号的代价不是少一行日志，而是停止阶段的状态根本没进表，
     * 停机卡在哪一步就看不出来了。
     */
    constexpr int kApplyNetworkServer = 1; /** 申请本机网络服务（Windows 的 WSAStartup） */
    constexpr int kCreateSocket = 2;       /** 创建监听 socket */
    constexpr int kBind = 3;               /** 绑定端口 */
    constexpr int kListen = 4;             /** 开始监听 */
    constexpr int kStartUp = 5;            /** 启动流水线汇总 */
    constexpr int kShard = 6;              /** 分片启动 */
    constexpr int kAcceptor = 7;           /** 接入线程启动 */
    constexpr int kShutdown = 8;           /** 网络服务停止（汇总） */
    constexpr int kStopAcceptor = 9;       /** 接入线程停止 */

    /*
     * C6 的停机三段。停机是「卡住 / 泄漏 / 丢请求」的高发区，所以每一步都要能在
     * `status` 里单独看见：看出来是「卡在排空」还是「卡在分片退出」，处理方式完全不同。
     */
    constexpr int kDrain = 10;     /** 等待在飞请求归零 + 排空写缓冲 */
    constexpr int kStopShard = 11; /** 分片停止 */
    constexpr int kReport = 12;    /** 最终统计上报 */
}

#endif // INKING_BACKEND_FRAMEWORK_NETWORK_STAGE_H
