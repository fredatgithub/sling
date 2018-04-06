// Copyright 2018 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sling/myelin/rnn.h"

#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"

namespace sling {
namespace myelin {

void BiLSTM::LSTM::Initialize(const Network &net, const string &name) {
  // Initialize LSTM cell.
  cell = net.GetCell(name);
  input = net.GetParameter(name + "/input");
  h_in = net.GetParameter(name + "/h_in");
  h_out = net.GetParameter(name + "/h_out");
  c_in = net.GetParameter(name + "/c_in");
  c_out = net.GetParameter(name + "/c_out");

  // Initialize gradient cell for LSTM.
  gcell = net.LookupCell("gradients/" + name);
  if (gcell != nullptr) {
    dinput = net.GetParameter("gradients/" + name + "/d_input");
    primal = net.GetParameter("gradients/" + name + "/primal");
    dh_in = net.GetParameter("gradients/" + name + "/d_h_in");
    dh_out = net.GetParameter("gradients/" + name + "/d_h_out");
    dc_in = net.GetParameter("gradients/" + name + "/d_c_in");
    dc_out = net.GetParameter("gradients/" + name + "/d_c_out");
  }
}

// Build flows for LSTMs.
BiLSTM::Outputs BiLSTM::Build(Flow *flow, const Library &library, int dim,
                              Flow::Variable *input, Flow::Variable *dinput) {
  Outputs out;

  // Build left-to-right LSTM flow.
  FlowBuilder lr(flow, name_ + "/lr");
  auto *lr_input = lr.Var("input", input->type, input->shape);
  lr_input->set_in();
  lr_input->ref = true;
  out.lr = lr.LSTMLayer(lr_input, dim);

  // Build right-to-left LSTM flow.
  FlowBuilder rl(flow, name_ + "/rl");
  auto *rl_input = rl.Var("input", input->type, input->shape);
  rl_input->set_in();
  rl_input->ref = true;
  out.rl = rl.LSTMLayer(rl_input, dim);

  // Connect input to LSTMs.
  flow->AddConnector(name_ + "/inputs", {input, lr_input, rl_input});

  // Build gradients for learning.
  if (dinput != nullptr) {
    auto *glr = Gradient(flow, lr.func(), library);
    auto *grl = Gradient(flow, rl.func(), library);
    out.dlr = flow->Var(glr->name + "d_input");
    out.drl = flow->Var(grl->name + "d_input");
    flow->AddConnector(name_ + "/inputs", {dinput, out.dlr, out.drl});
  } else {
    out.dlr = nullptr;
    out.drl = nullptr;
  }

  return out;
}

void BiLSTM::Initialize(const Network &net) {
  lr_.Initialize(net, name_ + "/lr");
  rl_.Initialize(net, name_ + "/rl");
}

BiLSTMInstance::BiLSTMInstance(const BiLSTM &bilstm)
    : bilstm_(bilstm),
      lr_(bilstm.lr_.cell),
      rl_(bilstm.rl_.cell),
      lr_hidden_(bilstm.lr_.h_in),
      lr_control_(bilstm.lr_.c_in),
      rl_hidden_(bilstm.rl_.h_in),
      rl_control_(bilstm.rl_.c_in) {}

BiChannel BiLSTMInstance::Compute(Channel *input) {
  // Reset hidden and control channels.
  int length = input->size();
  lr_hidden_.reset(length + 1);
  rl_hidden_.reset(length + 1);
  lr_control_.resize(length + 1);
  rl_control_.reset(length + 1);
  lr_control_.zero(length);
  rl_hidden_.zero(length);

  // Compute left-to-right LSTM.
  for (int i = 0; i < length; ++i) {
    // Attach hidden and control layers.
    int in = i > 0 ? i - 1 : length;
    int out = i;
    lr_.Set(bilstm_.lr_.h_in, &lr_hidden_, in);
    lr_.Set(bilstm_.lr_.h_out, &lr_hidden_, out);
    lr_.Set(bilstm_.lr_.c_in, &lr_control_, in);
    lr_.Set(bilstm_.lr_.c_out, &lr_control_, out);

    // Attach input features.
    lr_.Set(bilstm_.lr_.input, input, i);

    // Compute LSTM cell.
    lr_.Compute();
  }

  // Compute right-to-left LSTM.
  for (int i = 0; i < length; ++i) {
    // Attach hidden and control layers.
    int in = length - i;
    int out = in - 1;
    rl_.Set(bilstm_.rl_.h_in, &rl_hidden_, in);
    rl_.Set(bilstm_.rl_.h_out, &rl_hidden_, out);
    rl_.Set(bilstm_.rl_.c_in, &rl_control_, in);
    rl_.Set(bilstm_.rl_.c_out, &rl_control_, out);

    // Attach input features.
    rl_.Set(bilstm_.rl_.input, input, out);

    // Compute LSTM cell.
    rl_.Compute();
  }

  return BiChannel(&lr_hidden_, &rl_hidden_);
}

BiLSTMLearner::BiLSTMLearner(const BiLSTM &bilstm)
    : bilstm_(bilstm),
      lr_gradient_(bilstm.lr_.gcell),
      rl_gradient_(bilstm.rl_.gcell),
      lr_hidden_(bilstm.lr_.h_in),
      lr_control_(bilstm.lr_.c_in),
      rl_hidden_(bilstm.rl_.h_in),
      rl_control_(bilstm.rl_.c_in),
      dlr_hidden_(bilstm.lr_.dh_in),
      dlr_control_(bilstm.lr_.dc_in),
      drl_hidden_(bilstm.rl_.dh_in),
      drl_control_(bilstm.rl_.dc_in),
      dinput_(bilstm.rl_.dinput) {}

BiLSTMLearner::~BiLSTMLearner() {
  for (Instance *data : lr_) delete data;
  for (Instance *data : rl_) delete data;
}

BiChannel BiLSTMLearner::Compute(Channel *input) {
  // Allocate instances.
  int length = input->size();
  for (auto *data : lr_) delete data;
  for (auto *data : rl_) delete data;
  lr_.resize(length);
  rl_.resize(length);
  for (int i = 0; i < length; ++i) {
    lr_[i] = new Instance(bilstm_.lr_.cell);
    rl_[i] = new Instance(bilstm_.rl_.cell);
  }

  // Reset hidden and control channels.
  lr_hidden_.reset(length + 1);
  rl_hidden_.reset(length + 1);
  lr_control_.resize(length + 1);
  rl_control_.reset(length + 1);
  lr_control_.zero(length);
  rl_hidden_.zero(length);

  // Compute left-to-right LSTM.
  for (int i = 0; i < length; ++i) {
    // Attach hidden and control layers.
    Instance *lr = lr_[i];
    int in = i > 0 ? i - 1 : length;
    int out = i;
    lr->Set(bilstm_.lr_.h_in, &lr_hidden_, in);
    lr->Set(bilstm_.lr_.h_out, &lr_hidden_, out);
    lr->Set(bilstm_.lr_.c_in, &lr_control_, in);
    lr->Set(bilstm_.lr_.c_out, &lr_control_, out);

    // Attach input features.
    lr->Set(bilstm_.lr_.input, input, i);

    // Compute LSTM cell.
    lr->Compute();
  }

  // Compute right-to-left LSTM.
  for (int i = 0; i < length; ++i) {
    // Attach hidden and control layers.
    Instance *rl = rl_[i];
    int in = length - i;
    int out = in - 1;
    rl->Set(bilstm_.rl_.h_in, &rl_hidden_, in);
    rl->Set(bilstm_.rl_.h_out, &rl_hidden_, out);
    rl->Set(bilstm_.rl_.c_in, &rl_control_, in);
    rl->Set(bilstm_.rl_.c_out, &rl_control_, out);

    // Attach input features.
    rl->Set(bilstm_.rl_.input, input, out);

    // Compute LSTM cell.
    rl->Compute();
  }

  return BiChannel(&lr_hidden_, &rl_hidden_);
}

BiChannel BiLSTMLearner::PrepareGradientChannels(int length) {
  dlr_hidden_.reset(length + 1);
  drl_hidden_.reset(length + 1);
  dlr_control_.resize(length + 1);
  drl_control_.reset(length + 1);
  dlr_control_.zero(length);
  drl_hidden_.zero(length);
  dinput_.reset(length);

  return BiChannel(&dlr_hidden_, &drl_hidden_);
}

Channel *BiLSTMLearner::Backpropagate() {
  int length = lr_.size();

  // Propagate gradients for left-to-right LSTM.
  for (int i = length - 1; i >= 0; --i) {
    // Set reference to primal cell.
    lr_gradient_.Set(bilstm_.lr_.primal, lr_[i]);

    // Set gradient for hidden and control.
    lr_gradient_.Set(bilstm_.lr_.dh_out, &dlr_hidden_, i + 1);
    lr_gradient_.Set(bilstm_.lr_.dc_out, &dlr_control_, i + 1);
    lr_gradient_.Set(bilstm_.lr_.dh_in, &dlr_hidden_, i);
    lr_gradient_.Set(bilstm_.lr_.dc_in, &dlr_control_, i);

    // Set input gradient.
    lr_gradient_.Set(bilstm_.lr_.dinput, &dinput_, i);

    // Compute backward.
    lr_gradient_.Compute();
  }

  // Propagate gradients for right-to-left LSTM.
  for (int i = 0; i < length; ++i) {
    // Set reference to primal cell.
    rl_gradient_.Set(bilstm_.rl_.primal, rl_[i]);

    // Set gradient for hidden and control.
    int in = i > 0 ? i - 1 : length;
    int out = i;
    rl_gradient_.Set(bilstm_.rl_.dh_out, &drl_hidden_, in);
    rl_gradient_.Set(bilstm_.rl_.dc_out, &drl_control_, in);
    rl_gradient_.Set(bilstm_.rl_.dh_in, &drl_hidden_, out);
    rl_gradient_.Set(bilstm_.rl_.dc_in, &drl_control_, out);

    // Set input gradient.
    rl_gradient_.Set(bilstm_.rl_.dinput, &dinput_, i);

    // Compute backward.
    rl_gradient_.Compute();
  }

  // Return input gradient.
  return &dinput_;
}

}  // namespace myelin
}  // namespace sling

