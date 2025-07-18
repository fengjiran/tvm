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
#ifndef TVM_META_SCHEDULE_RUNNER_H_
#define TVM_META_SCHEDULE_RUNNER_H_

#include <tvm/ffi/container/array.h>
#include <tvm/ffi/function.h>
#include <tvm/ffi/optional.h>
#include <tvm/ffi/reflection/registry.h>
#include <tvm/ffi/string.h>
#include <tvm/ir/expr.h>
#include <tvm/meta_schedule/arg_info.h>
#include <tvm/node/reflection.h>
#include <tvm/runtime/object.h>

namespace tvm {
namespace meta_schedule {

/*! \brief Runner's input containing path of artifact, type of device and argument info. */
class RunnerInputNode : public runtime::Object {
 public:
  /*! \brief The path to the built artifact. */
  String artifact_path;
  /*! \brief The type of device. */
  String device_type;
  /*! \brief The argument information. */
  Array<ArgInfo> args_info;

  static void RegisterReflection() {
    namespace refl = tvm::ffi::reflection;
    refl::ObjectDef<RunnerInputNode>()
        .def_ro("artifact_path", &RunnerInputNode::artifact_path)
        .def_ro("device_type", &RunnerInputNode::device_type)
        .def_ro("args_info", &RunnerInputNode::args_info);
  }

  static constexpr const char* _type_key = "meta_schedule.RunnerInput";

  TVM_DECLARE_FINAL_OBJECT_INFO(RunnerInputNode, runtime::Object);
};

/*!
 * \brief Managed reference to RunnerInputNode
 * \sa RunnerInputNode
 */
class RunnerInput : public runtime::ObjectRef {
 public:
  /*!
   * \brief Constructor of RunnerInput
   * \param artifact_path The path to the built artifact.
   * \param device_type The type of device.
   * \param args_info The argument information.
   */
  TVM_DLL explicit RunnerInput(String artifact_path, String device_type, Array<ArgInfo> args_info);
  TVM_DEFINE_NOTNULLABLE_OBJECT_REF_METHODS(RunnerInput, runtime::ObjectRef, RunnerInputNode);
};

/*! \brief Runner's output containing measurement result of MeasureCandidate or error msg if any. */
class RunnerResultNode : public runtime::Object {
 public:
  /*! \brief The run time in seconds.*/
  Optional<Array<FloatImm>> run_secs;
  /*! \brief The error message, if any. */
  Optional<String> error_msg;

  static void RegisterReflection() {
    namespace refl = tvm::ffi::reflection;
    refl::ObjectDef<RunnerResultNode>()
        .def_ro("run_secs", &RunnerResultNode::run_secs)
        .def_ro("error_msg", &RunnerResultNode::error_msg);
  }

  static constexpr const char* _type_key = "meta_schedule.RunnerResult";

  TVM_DECLARE_FINAL_OBJECT_INFO(RunnerResultNode, runtime::Object);
};

/*!
 * \brief Managed reference to RunnerResultNode
 * \sa RunnerResultNode
 */
class RunnerResult : public runtime::ObjectRef {
 public:
  /*!
   * \brief Constructor
   * \brief The run time in seconds.
   * \brief The error message, if any.
   */
  TVM_DLL explicit RunnerResult(Optional<Array<FloatImm>> run_secs, Optional<String> error_msg);
  TVM_DEFINE_NOTNULLABLE_OBJECT_REF_METHODS(RunnerResult, runtime::ObjectRef, RunnerResultNode);
};

/*!
 * \brief A class to asynchronously fetch runner's output.
 * \note The API design is consistent with python's concurrent.futures.Future:
 *  https://docs.python.org/3/library/concurrent.futures.html#concurrent.futures.Future
 */
class RunnerFutureNode : public runtime::Object {
 public:
  /*!
   * \brief The function type to check whether the runner has finished.
   * \return Whether the runner's output is ready.
   */
  using FDone = ffi::TypedFunction<bool()>;
  /*!
   * \brief The function type to fetch runner output if it is ready.
   * \return The runner's output.
   */
  using FResult = ffi::TypedFunction<RunnerResult()>;

  /*! \brief The packed function to check whether the runner has finished. */
  FDone f_done;
  /*! \brief The packed function to fetch runner output if it is ready. */
  FResult f_result;

  static void RegisterReflection() {
    // `f_done` is not registered
    // `f_result` is not registered
  }

  /*!
   * \brief Check whether the runner has finished.
   * \return A boolean indicating whether the runner has finished.
   */
  bool Done() const {
    ICHECK(f_done != nullptr) << "PyRunnerFuture's Done method not implemented!";
    return f_done();
  }
  /*!
   * \brief Fetch the runner's output if it is ready.
   * \return The runner's output.
   */
  RunnerResult Result() const {
    ICHECK(f_result != nullptr) << "PyRunnerFuture's Result method not implemented!";
    return f_result();
  }

  static constexpr const char* _type_key = "meta_schedule.RunnerFuture";
  TVM_DECLARE_FINAL_OBJECT_INFO(RunnerFutureNode, runtime::Object);
};

/*!
 * \brief Managed reference to RunnerFutureNode
 * \sa RunnerFutureNode
 */
class RunnerFuture : public runtime::ObjectRef {
 public:
  using FDone = RunnerFutureNode::FDone;
  using FResult = RunnerFutureNode::FResult;

  /*!
   * \brief Constructor of RunnerFuture
   * \param f_done The packed function to check whether the runner has finished.
   * \param f_result The packed function to fetch runner output if it is ready.
   */
  TVM_DLL explicit RunnerFuture(FDone f_done, FResult f_result);
  TVM_DEFINE_MUTABLE_NOTNULLABLE_OBJECT_REF_METHODS(RunnerFuture, runtime::ObjectRef,
                                                    RunnerFutureNode);
};

/*! \brief The abstract runner interface. */
class RunnerNode : public runtime::Object {
 public:
  /*!
   * \brief The function type to run the built artifacts and get runner futures.
   * \param input The runner's inputs.
   * \return The runner futures.
   * \sa RunnerFuture
   */
  using FRun = ffi::TypedFunction<Array<RunnerFuture>(Array<RunnerInput>)>;

  /*! \brief Default destructor */
  virtual ~RunnerNode() = default;

  /*!
   * \brief Run the built artifact and get runner futures.
   * \param runner_inputs The runner's inputs.
   * \return The runner futures.
   */
  virtual Array<RunnerFuture> Run(Array<RunnerInput> runner_inputs) = 0;

  static constexpr const char* _type_key = "meta_schedule.Runner";
  TVM_DECLARE_BASE_OBJECT_INFO(RunnerNode, runtime::Object);
};

/*!
 * \brief Managed reference to RunnerNode
 * \sa RunnerNode
 */
class Runner : public runtime::ObjectRef {
 public:
  using FRun = RunnerNode::FRun;

  /*!
   * \brief Create a runner with customized build method on the python-side.
   * \param f_run The packed function to run the built artifacts and get runner futures.
   * \return The runner created.
   */
  TVM_DLL static Runner PyRunner(FRun f_run);
  TVM_DEFINE_MUTABLE_NOTNULLABLE_OBJECT_REF_METHODS(Runner, runtime::ObjectRef, RunnerNode);
};

/*! \brief An abstract runner with customized build method on the python-side. */
class PyRunnerNode : public RunnerNode {
 public:
  /*! \brief The packed function to run the built artifacts and get runner futures. */
  FRun f_run;

  static void RegisterReflection() {
    // `f_run` is not registered
  }

  Array<RunnerFuture> Run(Array<RunnerInput> runner_inputs) final {
    ICHECK(f_run != nullptr) << "PyRunner's Run method not implemented!";
    return f_run(runner_inputs);
  }

  static constexpr const char* _type_key = "meta_schedule.PyRunner";
  TVM_DECLARE_FINAL_OBJECT_INFO(PyRunnerNode, RunnerNode);
};

}  // namespace meta_schedule
}  // namespace tvm

#endif  // TVM_META_SCHEDULE_RUNNER_H_
