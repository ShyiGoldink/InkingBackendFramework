#ifndef INKING_BACKEND_FRAMEWORK_DATA_STRUCT_STAGE_STRUCT_H
#define INKING_BACKEND_FRAMEWORK_DATA_STRUCT_STAGE_STRUCT_H

#include <cstdint>
#include <string>
/**
 * @brief 自检阶段结构体。
 *
 * 该结构体用于存储 ShineBasicModule 的自检阶段状态。
 * 外部模块可通过 ShineBasicModule::getStage() 获取只读拷贝。
 *
 * 除了"当前是什么状态"，这里还保留"执行了多少次、第一次和最近一次是什么时候"，
 * 因为日志改成"只在状态变化时落盘"之后，重复执行的次数不再进日志，
 * 只能靠这三个字段回答"这个阶段到底跑没跑、跑了多少次"。
 */
struct Stage
{
    int step = 0;            /** 阶段编号 */
    std::string name;        /** 阶段名称 */
    bool status = true;      /** 当前阶段是否正常 */
    std::string message;     /** 阶段最近一次状态说明 */
    std::string description; /** 阶段说明 */
    std::string suggestion;  /** 错误建议 */

    std::uint64_t count = 0; /** 该阶段被更新过多少次（含重复的相同状态） */
    std::string firstSeen;   /** 第一次执行的时间，形如 19:34:40 */
    std::string lastSeen;    /** 最近一次执行的时间，形如 19:41:02 */
};

#endif // INKING_BACKEND_FRAMEWORK_DATA_STRUCT_STAGE_STRUCT_H
