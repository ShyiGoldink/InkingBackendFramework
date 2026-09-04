#ifndef INKING_BACKEND_FRAMEWORK_TASK_STRUCT_H
#define INKING_BACKEND_FRAMEWORK_TASK_STRUCT_H

#include <functional>
#include <memory>
#include <utility>
#include <vector>

template<typename T>
/**
 * Task是本项目的任务模式
 * 属性：
 * deps依赖任务/next后继任务/action动作
 * 方法：
 * dependOn(function);重载dependOn(vector<function>);如果上一个目标需要多个函数的返回值，那么传入函数vector，否则传入单个函数，避免参数错误/
 * then(function);重载then(vector<function>);这个没那么复杂，只是为了方便传入多个函数或者单个函数。
 * execute();对于函数中的每个节点，其实都能调用它执行，因为它会按照左中右的中序遍历，并根据依赖和后继完成入口。执行时，上个结点的全部返回值会按照顺序尝试传入下个结点的参数，请注意！
 */
struct Task{
    /**执行动作 */
    using Action = std::function<T(const std::vector<T>&)>; 

    Action action;                                        /**动作（中） */
    std::vector<std::unique_ptr<Task>> deps;              /**依赖（左，可有多个） */ 
    std::unique_ptr<Task> next;                           /**后继（右，最多一个） */

    /**
     * @brief 添加一个依赖：该依赖的返回值会成为本节点 action 的入参之一。
     */
    Task& dependOn(Action fn)
    {
        return dependOn(std::vector<Action>{std::move(fn)});
    }

    /**
     * @brief 添加多个依赖：按传入顺序，各自的返回值依次进入本节点 action 的参数。
     */
    Task& dependOn(std::vector<Action> fns)
    {
        for (auto &fn : fns)
        {
            auto dep = std::make_unique<Task>();
            dep->action = std::move(fn);
            deps.push_back(std::move(dep));
        }
        return *this;
    }

    /**
     * @brief 追加一个后继：本节点执行完后，把结果作为后继 action 的唯一入参。
     */
    Task& then(Action fn)
    {
        return then(std::vector<Action>{std::move(fn)});
    }

    /**
     * @brief 追加多个后继：按传入顺序串成一条链，前一个的输出进入后一个的入参。
     */
    Task& then(std::vector<Action> fns)
    {
        Task *tail = this;
        while (tail->next)
        {
            tail = tail->next.get();
        }

        for (auto &fn : fns)
        {
            auto step = std::make_unique<Task>();
            step->action = std::move(fn);
            tail->next = std::move(step);
            tail = tail->next.get();
        }
        return *this;
    }

    /**
     * @brief 执行本节点：按“左（依赖）→ 中（action）→ 右（后继）”的中序遍历完成。
     *
     * 上个结点（依赖们/上游节点）的全部返回值会按照顺序作为本节点 action
     * 的 vector 参数；action 的单个返回值再作为后继节点的入参继续传递。
     */
    T execute(const T& input) {
        // 1. 收集 action 的入参：
        //    没有依赖时，把上游传入的 input 作为唯一入参；
        //    有依赖时，每个依赖都独立执行并把结果按顺序聚合。
        std::vector<T> params;
        if (deps.empty())
        {
            params.push_back(input);
        }
        else
        {
            params.reserve(deps.size());
            for (auto &dep : deps)
            {
                params.push_back(dep->execute(input));
            }
        }

        // 2. 执行本节点动作；没有 action 的节点视为透传，原样传递 input
        T result = action ? action(params) : input;

        // 3. 把本节点结果继续传给后继
        if (next)
        {
            result = next->execute(result);
        }
        return result;
    }
};


#endif //INKING_BACKEND_FRAMEWORK_TASK_STRUCT_H
