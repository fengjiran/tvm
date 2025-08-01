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

/*!
 * \file cuDNN kernel calls for backward algorithms.
 */
#include <tvm/ffi/function.h>
#include <tvm/ffi/reflection/registry.h>
#include <tvm/runtime/data_type.h>
#include <tvm/runtime/device_api.h>

#include "cudnn_utils.h"

namespace tvm {
namespace contrib {

using namespace runtime;

void ConvolutionBackwardData(int mode, int format, int algo, int dims, int groups, const int pad[],
                             const int stride[], const int dilation[], DLTensor* dy, DLTensor* w,
                             DLTensor* dx, const std::string& conv_dtype) {
  CuDNNThreadEntry* entry_ptr = CuDNNThreadEntry::ThreadLocal();
  // Set Mode
  entry_ptr->conv_entry.mode = static_cast<cudnnConvolutionMode_t>(mode);
  SetConvDescriptors(entry_ptr, format, dims, groups, pad, stride, dilation, dx->shape, w->shape,
                     dy->shape, dy->dtype, conv_dtype);
  // Set Device
  entry_ptr->conv_entry.device = dy->device;
  // Set Algo
  entry_ptr->conv_entry.bwd_data_algo = static_cast<cudnnConvolutionBwdDataAlgo_t>(algo);

  // Set workspace
  size_t workspace_size = 0;
  CUDNN_CALL(cudnnGetConvolutionBackwardDataWorkspaceSize(
      entry_ptr->handle, entry_ptr->conv_entry.filter_desc, entry_ptr->conv_entry.output_desc,
      entry_ptr->conv_entry.conv_desc, entry_ptr->conv_entry.input_desc,
      entry_ptr->conv_entry.bwd_data_algo, &workspace_size));
  entry_ptr->conv_entry.UpdateWorkspace(workspace_size);
  CUDNN_CALL(cudnnConvolutionBackwardData(
      entry_ptr->handle, CuDNNDataType::GetConst<1>(entry_ptr->conv_entry.data_type),
      entry_ptr->conv_entry.filter_desc, w->data, entry_ptr->conv_entry.output_desc, dy->data,
      entry_ptr->conv_entry.conv_desc, entry_ptr->conv_entry.bwd_data_algo,
      entry_ptr->conv_entry.workspace, workspace_size,
      CuDNNDataType::GetConst<0>(entry_ptr->conv_entry.data_type), entry_ptr->conv_entry.input_desc,
      dx->data));
}

void BackwardDataFindAlgo(int format, int dims, int groups, const int pad[], const int stride[],
                          const int dilation[], const int dy_dim[], const int w_dim[],
                          const int dx_dim[], const std::string& data_dtype,
                          const std::string& conv_dtype, bool verbose, ffi::Any* ret) {
  CuDNNThreadEntry* entry_ptr = CuDNNThreadEntry::ThreadLocal();
  const int full_dims = dims + 2;
  std::vector<int64_t> dy_dim_int64(full_dims);
  std::vector<int64_t> w_dim_int64(full_dims);
  std::vector<int64_t> dx_dim_int64(full_dims);
  for (int i = 0; i < full_dims; ++i) {
    dy_dim_int64[i] = dy_dim[i];
    w_dim_int64[i] = w_dim[i];
    dx_dim_int64[i] = dx_dim[i];
  }
  SetConvDescriptors(entry_ptr, format, dims, groups, pad, stride, dilation, dx_dim_int64.data(),
                     w_dim_int64.data(), dy_dim_int64.data(), StringToDLDataType(data_dtype),
                     conv_dtype);

  int returned_algo_count = 0;

  cudnnConvolutionBwdDataAlgoPerf_t perf_results[CUDNN_CONVOLUTION_BWD_DATA_ALGO_COUNT];
  CUDNN_CALL(cudnnFindConvolutionBackwardDataAlgorithm(
      entry_ptr->handle, entry_ptr->conv_entry.filter_desc, entry_ptr->conv_entry.output_desc,
      entry_ptr->conv_entry.conv_desc, entry_ptr->conv_entry.input_desc,
      CUDNN_CONVOLUTION_BWD_DATA_ALGO_COUNT, &returned_algo_count, perf_results));

  const std::vector<std::string> bwd_data_algo_names{
      "CUDNN_CONVOLUTION_BWD_DATA_ALGO_0",  // non-deterministic
      "CUDNN_CONVOLUTION_BWD_DATA_ALGO_1",
      "CUDNN_CONVOLUTION_BWD_DATA_ALGO_FFT",
      "CUDNN_CONVOLUTION_BWD_DATA_ALGO_FFT_TILING",
      "CUDNN_CONVOLUTION_BWD_DATA_ALGO_WINOGRAD",
      "CUDNN_CONVOLUTION_BWD_DATA_ALGO_WINOGRAD_NONFUSED"};

  auto best_algo = perf_results[0].algo;
  if (verbose) {
    LOG(INFO) << "\tCUDNN Found " << returned_algo_count << " bwd data algorithms, choosing "
              << bwd_data_algo_names[best_algo];
    for (int i = 0; i < returned_algo_count; ++i) {
      LOG(INFO) << "\t\t" << i << ") " << bwd_data_algo_names[perf_results[i].algo]
                << " - time: " << perf_results[i].time << " ms"
                << ", Memory: " << perf_results[i].memory;
    }
  }
  ret[0] = static_cast<int>(best_algo);
}

void ConvolutionBackwardFilter(int mode, int format, int algo, int dims, int groups,
                               const int pad[], const int stride[], const int dilation[],
                               DLTensor* dy, DLTensor* x, DLTensor* dw,
                               const std::string& conv_dtype) {
  CuDNNThreadEntry* entry_ptr = CuDNNThreadEntry::ThreadLocal();
  // Set Mode
  entry_ptr->conv_entry.mode = static_cast<cudnnConvolutionMode_t>(mode);
  SetConvDescriptors(entry_ptr, format, dims, groups, pad, stride, dilation, x->shape, dw->shape,
                     dy->shape, x->dtype, conv_dtype);
  // Set Device
  entry_ptr->conv_entry.device = x->device;
  // Set Algo
  entry_ptr->conv_entry.bwd_filter_algo = static_cast<cudnnConvolutionBwdFilterAlgo_t>(algo);

  // Set workspace
  size_t workspace_size = 0;
  CUDNN_CALL(cudnnGetConvolutionBackwardFilterWorkspaceSize(
      entry_ptr->handle, entry_ptr->conv_entry.input_desc, entry_ptr->conv_entry.output_desc,
      entry_ptr->conv_entry.conv_desc, entry_ptr->conv_entry.filter_desc,
      entry_ptr->conv_entry.bwd_filter_algo, &workspace_size));
  entry_ptr->conv_entry.UpdateWorkspace(workspace_size);
  CUDNN_CALL(cudnnConvolutionBackwardFilter(
      entry_ptr->handle, CuDNNDataType::GetConst<1>(entry_ptr->conv_entry.data_type),
      entry_ptr->conv_entry.input_desc, x->data, entry_ptr->conv_entry.output_desc, dy->data,
      entry_ptr->conv_entry.conv_desc, entry_ptr->conv_entry.bwd_filter_algo,
      entry_ptr->conv_entry.workspace, workspace_size,
      CuDNNDataType::GetConst<0>(entry_ptr->conv_entry.data_type),
      entry_ptr->conv_entry.filter_desc, dw->data));
}

void BackwardFilterFindAlgo(int format, int dims, int groups, const int pad[], const int stride[],
                            const int dilation[], const int dy_dim[], const int x_dim[],
                            const int dw_dim[], const std::string& data_dtype,
                            const std::string& conv_dtype, bool verbose, ffi::Any* ret) {
  CuDNNThreadEntry* entry_ptr = CuDNNThreadEntry::ThreadLocal();
  const int full_dims = dims + 2;
  std::vector<int64_t> x_dim_int64(full_dims);
  std::vector<int64_t> dy_dim_int64(full_dims);
  std::vector<int64_t> dw_dim_int64(full_dims);
  for (int i = 0; i < full_dims; ++i) {
    x_dim_int64[i] = x_dim[i];
    dy_dim_int64[i] = dy_dim[i];
    dw_dim_int64[i] = dw_dim[i];
  }
  SetConvDescriptors(entry_ptr, format, dims, groups, pad, stride, dilation, x_dim_int64.data(),
                     dw_dim_int64.data(), dy_dim_int64.data(), StringToDLDataType(data_dtype),
                     conv_dtype);

  int returned_algo_count = 0;

  cudnnConvolutionBwdFilterAlgoPerf_t perf_results[CUDNN_CONVOLUTION_BWD_FILTER_ALGO_COUNT];
  CUDNN_CALL(cudnnFindConvolutionBackwardFilterAlgorithm(
      entry_ptr->handle, entry_ptr->conv_entry.input_desc, entry_ptr->conv_entry.output_desc,
      entry_ptr->conv_entry.conv_desc, entry_ptr->conv_entry.filter_desc,
      CUDNN_CONVOLUTION_BWD_FILTER_ALGO_COUNT, &returned_algo_count, perf_results));

  const std::vector<std::string> bwd_filter_algo_names{
      "CUDNN_CONVOLUTION_BWD_FILTER_ALGO_0",  // non-deterministic
      "CUDNN_CONVOLUTION_BWD_FILTER_ALGO_1",
      "CUDNN_CONVOLUTION_BWD_FILTER_ALGO_FFT",
      "CUDNN_CONVOLUTION_BWD_FILTER_ALGO_3",
      "CUDNN_CONVOLUTION_BWD_FILTER_ALGO_WINOGRAD_NONFUSED",
      "CUDNN_CONVOLUTION_BWD_FILTER_ALGO_FFT_TILING",
  };

  auto best_algo = perf_results[0].algo;
  if (verbose) {
    LOG(INFO) << "\tCUDNN Found " << returned_algo_count << " bwd filter algorithms, choosing "
              << bwd_filter_algo_names[best_algo];
    for (int i = 0; i < returned_algo_count; ++i) {
      LOG(INFO) << "\t\t" << i << ") " << bwd_filter_algo_names[perf_results[i].algo]
                << " - time: " << perf_results[i].time << " ms"
                << ", Memory: " << perf_results[i].memory;
    }
  }
  ret[0] = static_cast<int>(best_algo);
}

TVM_FFI_STATIC_INIT_BLOCK({
  namespace refl = tvm::ffi::reflection;
  refl::GlobalDef()
      .def_packed("tvm.contrib.cudnn.conv2d.backward_data",
                  [](ffi::PackedArgs args, ffi::Any* ret) {
                    int mode = args[0].cast<int>();
                    int format = args[1].cast<int>();
                    int algo = args[2].cast<int>();
                    int pad_v[2], stride_v[2], dilation_v[2];
                    for (int i = 0; i < 2; i++) {
                      pad_v[i] = args[3 + i].cast<int>();
                      stride_v[i] = args[5 + i].cast<int>();
                      dilation_v[i] = args[7 + i].cast<int>();
                    }
                    auto dy = args[9].cast<DLTensor*>();
                    auto w = args[10].cast<DLTensor*>();
                    auto dx = args[11].cast<DLTensor*>();
                    auto conv_dtype = args[12].cast<std::string>();
                    int groups = args[13].cast<int>();

                    ConvolutionBackwardData(mode, format, algo, 2, groups, pad_v, stride_v,
                                            dilation_v, dy, w, dx, conv_dtype);
                  })
      .def_packed("tvm.contrib.cudnn.conv.backward_data_find_algo",
                  [](ffi::PackedArgs args, ffi::Any* ret) {
                    int format = args[0].cast<int>();
                    int dims = args[1].cast<int>();
                    int* pad = static_cast<int*>(args[2].cast<void*>());
                    int* stride = static_cast<int*>(args[3].cast<void*>());
                    int* dilation = static_cast<int*>(args[4].cast<void*>());
                    int* dy_dim = static_cast<int*>(args[5].cast<void*>());
                    int* w_dim = static_cast<int*>(args[6].cast<void*>());
                    int* dx_dim = static_cast<int*>(args[7].cast<void*>());
                    auto data_dtype = args[8].cast<std::string>();
                    auto conv_dtype = args[9].cast<std::string>();
                    int groups = args[10].cast<int>();
                    bool verbose = args[11].cast<bool>();

                    BackwardDataFindAlgo(format, dims, groups, pad, stride, dilation, dy_dim, w_dim,
                                         dx_dim, data_dtype, conv_dtype, verbose, ret);
                  })
      .def_packed("tvm.contrib.cudnn.conv2d.backward_filter",
                  [](ffi::PackedArgs args, ffi::Any* ret) {
                    int mode = args[0].cast<int>();
                    int format = args[1].cast<int>();
                    int algo = args[2].cast<int>();
                    int pad_v[2], stride_v[2], dilation_v[2];
                    for (int i = 0; i < 2; i++) {
                      pad_v[i] = args[3 + i].cast<int>();
                      stride_v[i] = args[5 + i].cast<int>();
                      dilation_v[i] = args[7 + i].cast<int>();
                    }
                    auto dy = args[9].cast<DLTensor*>();
                    auto x = args[10].cast<DLTensor*>();
                    auto dw = args[11].cast<DLTensor*>();
                    auto conv_dtype = args[12].cast<std::string>();
                    int groups = args[13].cast<int>();

                    ConvolutionBackwardFilter(mode, format, algo, 2, groups, pad_v, stride_v,
                                              dilation_v, dy, x, dw, conv_dtype);
                  })
      .def_packed("tvm.contrib.cudnn.conv.backward_filter_find_algo",
                  [](ffi::PackedArgs args, ffi::Any* ret) {
                    int format = args[0].cast<int>();
                    int dims = args[1].cast<int>();
                    int* pad = static_cast<int*>(args[2].cast<void*>());
                    int* stride = static_cast<int*>(args[3].cast<void*>());
                    int* dilation = static_cast<int*>(args[4].cast<void*>());
                    int* dy_dim = static_cast<int*>(args[5].cast<void*>());
                    int* x_dim = static_cast<int*>(args[6].cast<void*>());
                    int* dw_dim = static_cast<int*>(args[7].cast<void*>());
                    auto data_dtype = args[8].cast<std::string>();
                    auto conv_dtype = args[9].cast<std::string>();
                    int groups = args[10].cast<int>();
                    bool verbose = args[11].cast<bool>();

                    BackwardFilterFindAlgo(format, dims, groups, pad, stride, dilation, dy_dim,
                                           x_dim, dw_dim, data_dtype, conv_dtype, verbose, ret);
                  });
});

}  // namespace contrib
}  // namespace tvm
