/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Distributed under BSD 3-Clause license.                                   *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Illinois Institute of Technology.                        *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of Hermes. The full Hermes copyright notice, including  *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the top directory. If you do not  *
 * have access to the file, you may request a copy from help@hdfgroup.org.   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "chimaera_admin/chimaera_admin.h"
#include "chimaera/api/chimaera_runtime.h"
#include "chimaera/work_orchestrator/comutex.h"
#include "chimaera/work_orchestrator/scheduler.h"

namespace chm::Admin {

class Server : public TaskLib {
 public:
  Task *queue_sched_;
  Task *proc_sched_;
  CoMutexTable<u32> mutexes_;

 public:
  Server() : queue_sched_(nullptr), proc_sched_(nullptr) {}

  /** Update number of lanes */
  void UpdateDomain(UpdateDomainTask *task, RunContext &rctx) {
    std::vector<UpdateDomainInfo> ops = task->ops_.vec();
    HRUN_RPC->UpdateDomains(ops);
    task->SetModuleComplete();
  }
  void MonitorUpdateDomain(u32 mode,
                           UpdateDomainTask *task,
                           RunContext &rctx) {
  }

  /** Register a task library dynamically */
  void RegisterTaskLib(RegisterTaskLibTask *task, RunContext &rctx) {
    std::string lib_name = task->lib_name_.str();
    CHI_TASK_REGISTRY->RegisterTaskLib(lib_name);
    task->SetModuleComplete();
  }
  void MonitorRegisterTaskLib(u32 mode,
                              RegisterTaskLibTask *task,
                              RunContext &rctx) {
  }

  /** Destroy a task library */
  void DestroyTaskLib(DestroyTaskLibTask *task, RunContext &rctx) {
    std::string lib_name = task->lib_name_.str();
    CHI_TASK_REGISTRY->DestroyTaskLib(lib_name);
    task->SetModuleComplete();
  }
  void MonitorDestroyTaskLib(u32 mode,
                             DestroyTaskLibTask *task,
                             RunContext &rctx) {
  }

  /** Create a task state */
  void CreateTaskState(CreateTaskStateTask *task, RunContext &rctx) {
    HILOG(kInfo, "REGISTERING kCreateTaskState for task {} on worker {} lane {} and state_name {}",
          task->task_node_, rctx.worker_id_, lane_id_, task->state_name_.str());
    ScopedCoMutexTable<u32> lock(mutexes_, 0, task, rctx);
    HILOG(kInfo, "BEGINNING kCreateTaskState for task {} on worker {} lane {}",
          task->task_node_, rctx.worker_id_, lane_id_);
    std::string lib_name = task->lib_name_.str();
    std::string state_name = task->state_name_.str();
    // Check local registry for task state
    bool state_existed = false;
    TaskState *task_state = CHI_TASK_REGISTRY->GetAnyTaskState(
        state_name, task->ctx_.id_);
    if (task_state) {
      task->ctx_.id_ = task_state->id_;
      state_existed = true;
      task->SetModuleComplete();
      HILOG(kInfo, "ENDING kCreateTaskState for task {} on worker {}",
            task->task_node_, rctx.worker_id_);
      return;
    }
    // Check global registry for task state
    if (task->ctx_.id_.IsNull()) {
      task->ctx_.id_ = CHI_TASK_REGISTRY->GetOrCreateTaskStateId(state_name);
    }
    // Create the task state
    HILOG(kInfo, "(node {}) Creating task state {} with id {} (task_node={})",
          CHI_CLIENT->node_id_, state_name, task->ctx_.id_, task->task_node_);
    if (task->ctx_.id_.IsNull()) {
      HELOG(kError, "(node {}) The task state {} with id {} is NULL.",
            CHI_CLIENT->node_id_, state_name, task->ctx_.id_);
      task->SetModuleComplete();
      return;
    }
    // Get # of lanes to create
    u32 global_lanes = task->ctx_.global_lanes_;
    u32 local_lanes_pn = task->ctx_.local_lanes_pn_;
    if (global_lanes == 0) {
      global_lanes = CHI_RPC->hosts_.size() * CHI_RUNTIME->GetNumLanes();
    }
    if (local_lanes_pn == 0) {
      local_lanes_pn = CHI_RUNTIME->GetNumLanes();
    }
    // Update the default domains for the state
    std::vector<UpdateDomainInfo> ops = CHI_RPC->CreateDefaultDomains(
        task->ctx_.id_,
        CHI_QM_CLIENT->admin_task_state_,
        task->scope_query_,
        global_lanes,
        local_lanes_pn);
    CHI_RPC->UpdateDomains(ops);
    std::vector<SubDomainId> lanes =
        CHI_RPC->GetLocalLanes(task->ctx_.id_);
    // Create the task state
    CHI_TASK_REGISTRY->CreateTaskState(
        lib_name.c_str(),
        state_name.c_str(),
        task->ctx_.id_,
        task, lanes);
    if (task->root_) {
      // Broadcast the state creation to all nodes
      TaskState *exec = CHI_TASK_REGISTRY->GetAnyTaskState(task->ctx_.id_);
      LPointer<Task> bcast;
      exec->CopyStart(Method::kCreate, task, bcast, true);
      auto *bcast_ptr = reinterpret_cast<CreateTaskStateTask *>(
          bcast.ptr_);
      bcast_ptr->task_node_ += 1;
      bcast_ptr->root_ = false;
      bcast_ptr->dom_query_ = bcast_ptr->scope_query_;
      bcast_ptr->method_ = Method::kCreateTaskState;
      bcast_ptr->task_state_ = CHI_ADMIN->id_;
      MultiQueue *queue =
          CHI_QM_CLIENT->GetQueue(CHI_QM_CLIENT->admin_queue_id_);
      bcast->YieldInit(task);
      queue->Emplace(bcast->prio_, bcast->GetLaneId(), bcast.shm_);
      task->Wait<TASK_YIELD_CO>(bcast);
      exec->Del(Method::kCreate, bcast.ptr_);
    }
    HILOG(kInfo, "ENDING kCreateTaskState for task {} on worker {} lane {}",
          task->task_node_, rctx.worker_id_, lane_id_);
    task->SetModuleComplete();
  }
  void MonitorCreateTaskState(u32 mode, CreateTaskStateTask *task,
                              RunContext &rctx) {
    switch (mode) {
      case MonitorMode::kReplicaAgg: {
        std::vector<LPointer<Task>> &replicas = *rctx.replicas_;
        auto replica = reinterpret_cast<CreateTaskStateTask *>(
            replicas[0].ptr_);
        task->ctx_ = replica->ctx_;
        HILOG(kDebug, "New aggregated task state {}", task->ctx_.id_);
      }
    }
  }

  /** Get task state id, fail if DNE */
  void GetTaskStateId(GetTaskStateIdTask *task, RunContext &rctx) {
    std::string state_name = task->state_name_.str();
    task->id_ = CHI_TASK_REGISTRY->GetTaskStateId(state_name);
    task->SetModuleComplete();
  }
  void MonitorGetTaskStateId(u32 mode,
                             GetTaskStateIdTask *task,
                             RunContext &rctx) {
    switch (mode) {
      case MonitorMode::kReplicaAgg: {
        std::vector<LPointer<Task>> &replicas = *rctx.replicas_;
        auto replica = reinterpret_cast<GetTaskStateIdTask *>(
            replicas[0].ptr_);
        task->id_ = replica->id_;
        HILOG(kDebug, "New aggregated task state {}", task->id_);
      }
    }
  }

  /** Destroy a task state */
  void DestroyTaskState(DestroyTaskStateTask *task, RunContext &rctx) {
    CHI_TASK_REGISTRY->DestroyTaskState(task->id_);
    task->SetModuleComplete();
  }
  void MonitorDestroyTaskState(u32 mode,
                               DestroyTaskStateTask *task,
                               RunContext &rctx) {
  }

  /** Stop this runtime */
  void StopRuntime(StopRuntimeTask *task, RunContext &rctx) {
    HILOG(kInfo, "Stopping (server mode)");
    HRUN_WORK_ORCHESTRATOR->FinalizeRuntime();
    HRUN_THALLIUM->StopThisDaemon();
    task->SetModuleComplete();
  }
  void MonitorStopRuntime(u32 mode, StopRuntimeTask *task, RunContext &rctx) {
  }

  /** Set work orchestrator policy */
  void SetWorkOrchQueuePolicy(SetWorkOrchQueuePolicyTask *task, RunContext &rctx) {
    if (queue_sched_) {
      queue_sched_->SetModuleComplete();
    }
    if (queue_sched_ && !queue_sched_->IsComplete()) {
      return;
    }
    auto queue_sched = CHI_CLIENT->NewTask<ScheduleTask>(
        task->task_node_,
        chm::DomainQuery::GetDirectHash(chm::SubDomainId::kLocalLaneSet, 0),
        task->policy_id_);
    queue_sched_ = queue_sched.ptr_;
    MultiQueue *queue = CHI_CLIENT->GetQueue(queue_id_);
    queue->Emplace(TaskPrio::kLowLatency, 0, queue_sched.shm_);
    task->SetModuleComplete();
  }
  void MonitorSetWorkOrchQueuePolicy(u32 mode,
                                     SetWorkOrchQueuePolicyTask *task,
                                     RunContext &rctx) {
  }

  /** Set work orchestration policy */
  void SetWorkOrchProcPolicy(SetWorkOrchProcPolicyTask *task,
                             RunContext &rctx) {
    if (proc_sched_) {
      proc_sched_->SetModuleComplete();
    }
    if (proc_sched_ && !proc_sched_->IsComplete()) {
      return;
    }
    auto proc_sched = CHI_CLIENT->NewTask<ScheduleTask>(
        task->task_node_,
        chm::DomainQuery::GetDirectHash(chm::SubDomainId::kLocalLaneSet, 0),
        task->policy_id_);
    proc_sched_ = proc_sched.ptr_;
    MultiQueue *queue = CHI_CLIENT->GetQueue(queue_id_);
    queue->Emplace(0, 0, proc_sched.shm_);
    task->SetModuleComplete();
  }
  void MonitorSetWorkOrchProcPolicy(u32 mode,
                                    SetWorkOrchProcPolicyTask *task,
                                    RunContext &rctx) {
  }

  /** Flush the runtime */
  void Flush(FlushTask *task, RunContext &rctx) {
    if (!rctx.flush_->flushing_) {
      task->SetModuleComplete();
    }
  }
  void MonitorFlush(u32 mode, FlushTask *task, RunContext &rctx) {
    switch (mode) {
      case MonitorMode::kReplicaAgg: {
        std::vector<LPointer<Task>> &replicas = *rctx.replicas_;
        auto replica = reinterpret_cast<FlushTask *>(
            replicas[0].ptr_);
        task->work_done_ += replica->work_done_;
        HILOG(kInfo, "Total work done in this task: {}", task->work_done_);
      }
    }
  }

  /** Get the domain size */
  void GetDomainSize(GetDomainSizeTask *task, RunContext &rctx) {
    task->dom_size_ =
        HRUN_RPC->GetDomainSize(task->dom_id_);
    task->SetModuleComplete();
  }
  void MonitorGetDomainSize(u32 mode, GetDomainSizeTask *task, RunContext &rctx) {
  }

 public:
#include "chimaera_admin/chimaera_admin_lib_exec.h"
};

}  // namespace chm

HRUN_TASK_CC(chm::Admin::Server, "chimaera_admin");
