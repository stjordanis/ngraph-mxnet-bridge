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
#include <vector>

#include "../../../../src/operator/nn/convolution-inl.h"
#include "convolution.h"

#include <ngraph/builder/quantization.hpp>
#include "../ngraph_emitter.h"
#include "../ngraph_emitter_utils.h"
#include "../ngraph_nnvm_ops.h"
#include "../ngraph_sgcompiler_utils.h"
#include "../ngraph_utils.h"

#if MXNET_USE_MKLDNN == 1
#include "../../../../src/operator/subgraph/mkldnn/mkldnn_conv-inl.h"
#endif

namespace ngraph_bridge {

struct ConvInputs {
  NgraphNodePtr data;
  NgraphNodePtr filter;
  NgraphNodePtr bias;
  ngraph::CoordinateDiff pad;
  ngraph::Strides stride;
  ngraph::Strides dilate;
  size_t groups;
};

ConvInputs get_conv_inputs(NgraphNodePtr data, NgraphNodePtr filter,
                           NgraphNodePtr bias,
                           const mxnet::op::ConvolutionParam& param) {
  ConvInputs conv_inputs;
  conv_inputs.data = data;
  conv_inputs.filter = filter;
  conv_inputs.bias = bias;
  conv_inputs.pad = ngraph::CoordinateDiff{param.pad.begin(), param.pad.end()};
  conv_inputs.stride =
      ngraph::Strides{param.stride.begin(), param.stride.end()};
  conv_inputs.dilate =
      ngraph::Strides{param.dilate.begin(), param.dilate.end()};
  conv_inputs.groups = param.num_group;
  return conv_inputs;
}
ConvInputs get_conv_inputs(Emitter* emitter, const NodePtr& node,
                           const mxnet::op::ConvolutionParam& param) {
  auto data = emitter->op_map_[node->inputs_[0]];
  auto filter = emitter->op_map_[node->inputs_[1]];
  NgraphNodePtr bias =
      param.no_bias ? nullptr : emitter->op_map_[node->inputs_[2]];
  return get_conv_inputs(data, filter, bias, param);
}

NgraphNodePtr create_convolution(const ConvInputs& conv_inputs) {
  auto dshape = conv_inputs.data->get_shape();
  auto fshape = conv_inputs.filter->get_shape();

  NgraphNodePtr convolution = nullptr;
  if (conv_inputs.groups == 1) {
    convolution = std::make_shared<ngraph::op::Convolution>(
        conv_inputs.data, conv_inputs.filter, conv_inputs.stride,
        conv_inputs.dilate, conv_inputs.pad, conv_inputs.pad);
  } else {
    std::vector<NgraphNodePtr> convolutions(conv_inputs.groups);
    for (size_t g = 0; g < conv_inputs.groups; ++g) {
      // slice data on channel_in
      size_t data_slice_step = dshape[1] / conv_inputs.groups;
      size_t filter_slice_step = fshape[0] / conv_inputs.groups;
      auto data_slice = slice_data_on_axis(
          conv_inputs.data, g * data_slice_step, data_slice_step, 1, false);
      auto filter_slice =
          slice_data_on_axis(conv_inputs.filter, g * filter_slice_step,
                             filter_slice_step, 0, false);
      // convolve sliced data and filter
      // N, channel_out/groups, d'1,...,d'n
      convolutions[g] = std::make_shared<ngraph::op::Convolution>(
          data_slice, filter_slice, conv_inputs.stride, conv_inputs.dilate,
          conv_inputs.pad, conv_inputs.pad);
    }

    // concatenate convolutions on channel_out
    // N, channel_out, d'1,...,d'n
    convolution = std::make_shared<ngraph::op::Concat>(convolutions, 1);
  }

  // no bias param, return
  if (!conv_inputs.bias) {
    return convolution;
  }

  // 1, channel_out, 1,...,1
  ngraph::Shape bias_shape(fshape.size(), 1);
  bias_shape[1] = fshape[0];

  ngraph::AxisVector order(1, 0);
  auto bias_reshape = std::make_shared<ngraph::op::Reshape>(conv_inputs.bias,
                                                            order, bias_shape);

  return ngraph::builder::make_with_numpy_broadcast<ngraph::op::Add>(
      convolution, bias_reshape);
}

NgraphNodePtr create_convolution(Emitter* emitter, const NodePtr& node) {
  const auto& param =
      nnvm::get<mxnet::op::ConvolutionParam>(node->orig_node_->attrs.parsed);
  auto conv_inputs = get_conv_inputs(emitter, node, param);
  return create_convolution(conv_inputs);
}

#if MXNET_USE_MKLDNN == 1
NgraphNodePtr create_sgmkldnn_conv(Emitter* emitter, const NodePtr& node) {
  const auto& param_ = nnvm::get<mxnet::op::MKLDNNConvFusionParam>(
      node->orig_node_->attrs.parsed);
  auto& full_conv_param = param_.full_conv_param;
  auto& mkldnn_param = full_conv_param.mkldnn_param;
  auto& conv_param = full_conv_param.conv_param;
  auto bn_param = param_.bn_param.get();
  auto conv_inputs = get_conv_inputs(emitter, node, conv_param);
  auto output = create_convolution(conv_inputs);
  if (mkldnn_param.with_bn) {
    enum InputName { kData = 1, kGamma, kBeta, kMovingMean, kMovingVar };
    auto ng_in_gamma = emitter->op_map_[node->inputs_[kGamma]];
    auto ng_in_beta = emitter->op_map_[node->inputs_[kBeta]];
    auto ng_in_moving_mean = emitter->op_map_[node->inputs_[kMovingMean]];
    auto ng_in_moving_var = emitter->op_map_[node->inputs_[kMovingVar]];
    auto ng_actual_gamma =
        bn_param->fix_gamma
            ? makeConstant(ng_in_moving_mean->get_element_type(),
                           ng_in_moving_mean->get_shape(), 1)
            : ng_in_gamma;
    output = std::make_shared<ngraph::op::BatchNormInference>(
        output, ng_actual_gamma, ng_in_beta, ng_in_moving_mean,
        ng_in_moving_var, bn_param->eps);
  }
  if (mkldnn_param.with_relu) {
    output = std::make_shared<ngraph::op::Relu>(output);
  }
  return output;
}
NgraphNodePtr create_quantized_convolution(Emitter* emitter,
                                           const NodePtr& node) {
  // handle non-quantized sg_mkldnn_conv op, has 1 output
  if (node->orig_node_->num_outputs() < 2) {
    return create_sgmkldnn_conv(emitter, node);
  }

  // handle quantized sg_mkldnn_conv op, has 3 outputs
  NgraphNodePtr op;
  if (node->multi_output_index_ >= 0) {
    return emitter->multi_output_map_.at(node->inputs_[0])
        .at(node->multi_output_index_);
  }

  const auto& param_ = nnvm::get<mxnet::op::MKLDNNConvFusionParam>(
      node->orig_node_->attrs.parsed);
  auto& full_conv_param = param_.full_conv_param;
  auto& mkldnn_param = full_conv_param.mkldnn_param;
  auto& conv_param = full_conv_param.conv_param;
  auto bn_param = param_.bn_param.get();

  size_t idx = 0;
  auto in_data = idx++;
  auto in_weight = idx++;
  auto in_bias = conv_param.no_bias ? 0 : (idx++);
  auto in_gamma = mkldnn_param.with_bn ? (idx++) : 0;
  auto in_beta = mkldnn_param.with_bn ? (idx++) : 0;
  auto in_mean = mkldnn_param.with_bn ? (idx++) : 0;
  auto in_var = mkldnn_param.with_bn ? (idx++) : 0;
  auto in_sum = mkldnn_param.with_sum ? (idx++) : 0;
  auto in_min = idx++;
  auto in_max = idx++;
  auto in_sum_min = mkldnn_param.with_sum ? (idx++) : 0;
  auto in_sum_max = mkldnn_param.with_sum ? (idx++) : 0;

  auto data = emitter->op_map_[node->inputs_[in_data]];
  auto filter = emitter->op_map_[node->inputs_[in_weight]];
  auto bias =
      (in_bias == 0) ? nullptr : emitter->op_map_[node->inputs_[in_bias]];
  auto conv_inputs = get_conv_inputs(data, filter, bias, conv_param);
  if (conv_inputs.groups != 1) {
    throw std::runtime_error("group>1 not supported by quantized_conv for now");
  }
  auto fshape = conv_inputs.filter->get_shape();
  auto arg1 = std::make_shared<ngraph::op::Reshape>(
      emitter->op_map_[node->inputs_[in_min]], ngraph::AxisVector{0},
      ngraph::Shape{});
  auto arg2 = std::make_shared<ngraph::op::Reshape>(
      emitter->op_map_[node->inputs_[in_max]], ngraph::AxisVector{0},
      ngraph::Shape{});
  auto filter_n =
      std::make_shared<ngraph::op::Min>(filter, ngraph::AxisSet{0, 1, 2, 3});
  auto filter_m =
      std::make_shared<ngraph::op::Max>(filter, ngraph::AxisSet{0, 1, 2, 3});
  if (in_bias != 0) {
    ngraph::Shape bias_shape(fshape.size(), 1);
    bias_shape[1] = fshape[0];
    ngraph::AxisVector order(1, 0);
    bias = std::make_shared<ngraph::op::Reshape>(conv_inputs.bias, order,
                                                 bias_shape);
  }
  auto round_mode = ngraph::op::Quantize::RoundMode::ROUND_NEAREST_TOWARD_EVEN;
  auto qfilter = ngraph::builder::ScaledQuantize(conv_inputs.filter, filter_n,
                                                 filter_m, ngraph::element::i8,
                                                 ngraph::AxisSet{}, round_mode);
  auto qbias = ngraph::builder::ScaledQuantize(bias, filter_n, filter_m,
                                               ngraph::element::i32,
                                               ngraph::AxisSet{}, round_mode);
  auto min = makeConstant(ngraph::element::f32, ngraph::Shape{},
                          std::to_string(mkldnn_param.min_calib_range.value()));
  auto max = makeConstant(ngraph::element::f32, ngraph::Shape{},
                          std::to_string(mkldnn_param.max_calib_range.value()));
  op = ngraph::builder::ScaledQuantizedConvolutionBias(
      conv_inputs.data, qfilter, qbias, conv_inputs.stride, conv_inputs.dilate,
      conv_inputs.pad, conv_inputs.pad, conv_inputs.dilate, arg1, arg2,
      filter_n, filter_m, min, max, true);
  emitter->multi_output_map_[node] = {op, arg1, arg2};

  return op;
}
NgraphNodePtr create_quantized_convolution1(Emitter* emitter,
                                            const NodePtr& node) {
  const auto& param_ = nnvm::get<mxnet::op::MKLDNNConvFusionParam>(
      node->orig_node_->attrs.parsed);
  auto& full_conv_param = param_.full_conv_param;
  auto& mkldnn_param = full_conv_param.mkldnn_param;
  auto& conv_param = full_conv_param.conv_param;
  auto bn_param = param_.bn_param.get();
  auto conv_inputs = get_conv_inputs(emitter, node, conv_param);
  if (conv_inputs.groups != 1 || !conv_inputs.bias) {
    throw std::runtime_error(
        "nobias or group>1 not supported by quantized_conv for now");
  }
  auto fshape = conv_inputs.filter->get_shape();
  auto data_n = emitter->op_map_[node->inputs_[3]];
  auto data_m = emitter->op_map_[node->inputs_[4]];
  auto filter_n = emitter->op_map_[node->inputs_[5]];
  auto filter_m = emitter->op_map_[node->inputs_[6]];
  NgraphNodePtr bias, bias_n, bias_m;
  if (conv_inputs.bias) {
    bias_n = emitter->op_map_[node->inputs_[3]];
    bias_m = emitter->op_map_[node->inputs_[4]];
    ngraph::Shape bias_shape(fshape.size(), 1);
    bias_shape[1] = fshape[0];
    ngraph::AxisVector order(1, 0);
    bias = std::make_shared<ngraph::op::Reshape>(conv_inputs.bias, order,
                                                 bias_shape);
  }
  auto min = makeConstant(ngraph::element::f32, ngraph::Shape{},
                          std::to_string(mkldnn_param.min_calib_range.value()));
  auto max = makeConstant(ngraph::element::f32, ngraph::Shape{},
                          std::to_string(mkldnn_param.max_calib_range.value()));

  auto conv = ngraph::builder::ScaledQuantizedConvolutionBias(
      conv_inputs.data, conv_inputs.filter, bias, conv_inputs.stride,
      conv_inputs.dilate, conv_inputs.pad, conv_inputs.pad, conv_inputs.dilate,
      data_n, data_m, filter_n, filter_m, min, max);
  return conv;
}
#endif
}
