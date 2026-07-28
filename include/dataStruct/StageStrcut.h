#ifndef INKING_BACKEND_FRAMEWORK_DATA_STRUCT_STAGE_STRUCT_H
#define INKING_BACKEND_FRAMEWORK_DATA_STRUCT_STAGE_STRUCT_H

#include <string>
/**
 * @brief 自检阶段结构体。
 *
 * 该结构体用于存储 ShineBasicModule 的自检阶段状态。
 * 外部模块可通过 ShineBasicModule::getStage() 获取只读引用。
 */
struct Stage
{
    int step = 0;            /** 阶段编号 */
    std::string name;        /** 阶段名称 */
    bool status = true;      /** 当前阶段是否正常 */
    std::string message;     /** 当前阶段最近一次状态说明 */
    std::string description; /** 阶段说明 */
    std::string suggestion;  /** 错误建议 */
};

#endif // INKING_BACKEND_FRAMEWORK_DATA_STRUCT_STAGE_STRUCT_H
