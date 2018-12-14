/*******************************************************************************
* Copyright 2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef MXNET_NGRAPH_OPS_CONVOLUTION_H_
#define MXNET_NGRAPH_OPS_CONVOLUTION_H_

#include "../../../../src/operator/nn/convolution-inl.h"
#include "../ngraph_emitter.h"

namespace ngraph_bridge {
NgraphNodePtr create_convolution(Emitter* emitter, const NodePtr& node);
#if MXNET_USE_MKLDNN == 1
NgraphNodePtr create_quantized_convolution(Emitter* emitter,
                                           const NodePtr& node);
#endif
}  // namespace ngraph_bridge

#endif  // MXNET_NGRAPH_OPS_CONVOLUTION_H_
