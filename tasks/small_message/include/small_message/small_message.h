//
// Created by lukemartinlogan on 6/29/23.
//

#ifndef HRUN_small_message_H_
#define HRUN_small_message_H_

#include "small_message_tasks.h"

namespace chi::small_message {

/** Create admin requests */
class Client : public ModuleClient {

 public:
  /** Default constructor */
  Client() = default;

  /** Destructor */
  ~Client() = default;

  /** Create a small_message */
  void AsyncCreateConstruct(CreateTask *task,
                            const TaskNode &task_node,
                            const DomainQuery &dom_query,
                            const DomainQuery &affinity,
                            const std::string &pool_name,
                            const CreateContext &ctx) {
    CHI_CLIENT->ConstructTask<CreateTask>(
        task, task_node, dom_query, affinity, pool_name, ctx);
  }
  void Create(const DomainQuery &dom_query,
                  const DomainQuery &affinity,
                  const std::string &pool_name,
                  const CreateContext &ctx = CreateContext()) {
    LPointer<CreateTask> task = AsyncCreate(
        dom_query, affinity, pool_name, ctx);
    task->Wait();
    Init(task->ctx_.id_);
    CHI_CLIENT->DelTask(task);
  }
  CHI_TASK_METHODS(Create);

  /** Destroy state + queue */
  HSHM_ALWAYS_INLINE
  void Destroy(const DomainQuery &dom_query) {
    CHI_ADMIN->DestroyContainer(dom_query, id_);
  }

  /** Metadata task */
  void AsyncMdConstruct(MdTask *task,
                        const TaskNode &task_node,
                        const DomainQuery &dom_query,
                        u32 depth, u32 flags) {
    CHI_CLIENT->ConstructTask<MdTask>(
        task, task_node, dom_query, id_, depth, flags);
  }
  int Md(const DomainQuery &dom_query, u32 depth, u32 flags) {
    LPointer<MdTask> task =
        AsyncMd(dom_query, depth, flags);
    task->Wait();
    int ret = task->ret_;
    CHI_CLIENT->DelTask(task);
    return ret;
  }
  CHI_TASK_METHODS(Md);

  /** Io task */
  void AsyncIoConstruct(IoTask *task, const TaskNode &task_node,
                        const DomainQuery &dom_query,
                        size_t io_size,
                        u32 io_flags) {
    CHI_CLIENT->ConstructTask<IoTask>(
        task, task_node, dom_query, id_, io_size, io_flags);
  }
  void Io(const DomainQuery &dom_query, size_t io_size,
             u32 io_flags, size_t &write_ret, size_t &read_ret) {
    LPointer<IoTask> task =
        AsyncIo(dom_query, io_size, io_flags);
    task->Wait();
    write_ret = task->ret_;
    char *data = CHI_CLIENT->GetDataPointer(task->data_);
    read_ret = 0;
    for (size_t i = 0; i < io_size; ++i) {
      read_ret += data[i];
    }
    CHI_CLIENT->DelTask(task);
  }
  CHI_TASK_METHODS(Io)
};

}  // namespace chi

#endif  // HRUN_small_message_H_
