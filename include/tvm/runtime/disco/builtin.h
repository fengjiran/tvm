/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#ifndef TVM_RUNTIME_DISCO_BUILTIN_H_
#define TVM_RUNTIME_DISCO_BUILTIN_H_

#include <tvm/runtime/data_type.h>
#include <tvm/runtime/module.h>
#include <tvm/runtime/ndarray.h>

#include <string>

namespace tvm {
namespace runtime {

/*!
 * \brief Possible kinds of reduction operations.
 */
enum class ReduceKind : int32_t {
  kSum = 0,
  kProd = 1,
  kMin = 2,
  kMax = 3,
  kAvg = 4,
};

/*! \brief Converts `ReduceKind` to string */
inline std::string ReduceKind2String(ReduceKind kind) {
  switch (kind) {
    case ReduceKind::kSum:
      return "kSum";
    case ReduceKind::kProd:
      return "kProd";
    case ReduceKind::kMin:
      return "kMin";
    case ReduceKind::kMax:
      return "kMax";
    case ReduceKind::kAvg:
      return "kAvg";
  }
  LOG(FATAL) << "ValueError: Unknown ReduceKind: " << static_cast<int>(kind);
}

/*!
 * \brief Load a runtime Module, then create and initialize a RelaxVM
 * \param path The path to the runtime Module (a DSO file) to be loaded
 * \param device The default device used to initialize the RelaxVM
 * \return The RelaxVM as a runtime Module
 */
TVM_DLL Module LoadVMModule(std::string path, Optional<Device> device);
/*!
 * \brief Create an uninitialized empty NDArray
 * \param shape The shape of the NDArray
 * \param dtype The dtype of the NDArray
 * \param device The device the NDArray is created on. If None, use the thread local default device
 * \return The NDArray created
 */
TVM_DLL NDArray DiscoEmptyNDArray(ffi::Shape shape, DataType dtype, Optional<Device> device);
/*!
 * \brief Perform an allreduce operation using the underlying communication library
 * \param send The array send to perform allreduce on
 * \param reduce_kind The kind of reduction operation (e.g. sum, avg, min, max)
 * \param in_group Whether the allreduce operation performs globally or in group as default.
 * \param recv The array receives the outcome of allreduce
 */
TVM_DLL void AllReduce(NDArray send, ReduceKind reduce_kind, bool in_group, NDArray recv);
/*!
 * \brief Perform an allgather operation using the underlying communication library
 * \param send The array send to perform allgather on
 * \param in_group Whether the allgather operation performs globally or in group as default.
 * \param recv The array receives the outcome of allgather
 */
TVM_DLL void AllGather(NDArray send, bool in_group, NDArray recv);
/*!
 * \brief Perform a broadcast operation from worker-0
 * \param send The buffer to be broadcasted
 * \param in_group Whether the broadcast operation performs globally or in group as default.
 * \param recv The buffer receives the broadcasted array
 */
TVM_DLL void BroadcastFromWorker0(NDArray send, bool in_group, NDArray recv);
/*!
 * \brief Perform a scatter operation from worker-0, chunking the given buffer into equal parts.
 * \param send For worker-0, it must be provided, and otherwise, the buffer must be None.
 * The buffer will be divided into equal parts and sent to each worker accordingly.
 * \param in_group Whether the scatter operation performs globally or in group as default.
 * \param recv The receiving buffer, which must not be None.
 */
TVM_DLL void ScatterFromWorker0(Optional<NDArray> send, bool in_group, NDArray recv);
/*!
 * \brief Perform a gather operation to worker-0.
 * \param send The sending buffer, which must not be None.
 * \param in_group Whether the gather operation performs globally or in group as default.
 * \param recv For worker-0, it must be provided, and otherwise, the buffer must be None. The
 * receiving buffer will be divided into equal parts and receive from each worker accordingly.
 */
TVM_DLL void GatherToWorker0(NDArray send, bool in_group, Optional<NDArray> recv);
/*!
 * \brief Receive a buffer from worker-0. No-op if the current worker is worker-0.
 * \param buffer The buffer to be received
 */
TVM_DLL void RecvFromWorker0(NDArray buffer);
/*!
 * \brief Send a buffer to the corresponding worker in the next group.
 * An error is thrown if the worker is already in the last group.
 * \param buffer The sending buffer.
 */
TVM_DLL void SendToNextGroup(NDArray buffer);
/*!
 * \brief Receive a buffer from the corresponding worker in the previous group.
 * An error is thrown if the worker is already in the first group.
 * \param buffer The receiving buffer.
 */
TVM_DLL void RecvFromPrevGroup(NDArray buffer);
/*!
 * \brief Send a buffer to the target receiver worker (globally across all groups).
 * \param buffer The sending buffer.
 * \param receiver_id The global receiver worker id.
 */
TVM_DLL void SendToWorker(NDArray buffer, int receiver_id);
/*!
 * \brief Receive a buffer from the target sender worker (globally across all groups).
 * \param buffer The receiving buffer.
 * \param sender_id The global sender worker id.
 */
TVM_DLL void RecvFromWorker(NDArray buffer, int sender_id);
/*! \brief Get the local worker id */
TVM_DLL int WorkerId();
/*!
 * \brief Called by the worker thread. Waiting until the worker completes all its tasks.
 * As a specific example, on a CUDA worker, it blocks until all kernels are launched and
 * cudaStreamSynchronize is complete.
 */
TVM_DLL void SyncWorker();

}  // namespace runtime
}  // namespace tvm

#endif  // TVM_RUNTIME_DISCO_BUILTIN_H_
