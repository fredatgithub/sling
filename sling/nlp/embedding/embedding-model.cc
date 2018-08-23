// Copyright 2017 Google Inc.
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

#include "sling/nlp/embedding/embedding-model.h"

#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"
#include "sling/util/random.h"

namespace sling {
namespace nlp {

using namespace myelin;

void MikolovFlow::Build() {
  BuildModel();
  BuildLayer0();
  BuildLayer1();
  BuildLayer0Back();
}

void MikolovFlow::BuildModel() {
  W0 = AddWeights("W0", DT_FLOAT, {inputs, dims});
  W1 = AddWeights("W1", DT_FLOAT, {outputs, dims});
  W0->set_random();
}

void MikolovFlow::BuildLayer0() {
  layer0 = AddFunction("layer0");
  FlowBuilder tf(this, layer0);

  fv = tf.Var("features", DT_INT32, {1, in_features});
  hidden = tf.Name(tf.GatherAvg(W0, fv), "hidden");
}

void MikolovFlow::BuildLayer1() {
  layer1 = AddFunction("layer1");
  FlowBuilder tf(this, layer1);

  // Inputs.
  alpha = tf.Var("alpha", DT_FLOAT, {});
  label = tf.Var("label", DT_FLOAT, {1, 1});
  target = tf.Var("target", DT_INT32, {1, out_features});
  error = tf.Var("error", DT_FLOAT, {dims});
  l1_l0 = tf.Instance(layer0);
  auto *h = tf.Ref(l1_l0, hidden);

  // Output.
  bool single = out_features == 1;
  auto *embed = single ? tf.Gather(W1, target) : tf.GatherAvg(W1, target);
  auto *output = tf.Dot(embed, h, dims);

  // Loss.
  loss = tf.Name(tf.Sub(label, tf.Sigmoid(output)), "loss");
  loss->set_out();
  auto *eta = tf.Mul(loss, alpha);

  // Backprop layer 1.
  tf.AssignAdd(error, tf.Mul(embed, eta));
  tf.ScatterAdd(W1, target, tf.Mul(h, eta));
}

void MikolovFlow::BuildLayer0Back() {
  layer0b = AddFunction("layer0b");
  FlowBuilder tf(this, layer0b);

  l0b_l0 = tf.Instance(layer0);
  l0b_l1 = tf.Instance(layer1);
  tf.ScatterAdd(W0, tf.Ref(l0b_l0, fv), tf.Ref(l0b_l1, error));
}

void DualEncoderFlow::Build(const Transformations &library) {
  // Create left and right encoders.
  left.name = name + "/left";
  BuildEncoder(&left);
  right.name = name + "/right";
  BuildEncoder(&right);

  // Create similarity computation.
  sim = AddFunction(name + "/similarity");
  FlowBuilder tf(this, sim);
  sim_left = tf.Placeholder("left", DT_FLOAT, {batch_size, dims});
  sim_left->set_unique();
  sim_right = tf.Placeholder("right", DT_FLOAT, {batch_size, dims});
  sim_right->set_unique();
  sim_cosine = tf.Name(tf.MatMul(sim_left, tf.Transpose(sim_right)), "cosine");

  // Create gradient computations.
  left.backward = Gradient(this, left.forward, library);
  right.backward = Gradient(this, right.forward, library);
  gsim = Gradient(this, sim, library);

  gsim_d_cosine = GradientVar(sim_cosine);
  gsim_d_left = GradientVar(sim_left);
  gsim_d_right = GradientVar(sim_right);
  left.d_encoding = GradientVar(left.encoding);
  right.d_encoding = GradientVar(right.encoding);

  gsim_primal = PrimalVar(sim);
  left.primal = PrimalVar(left.forward);
  right.primal = PrimalVar(right.forward);
}

void DualEncoderFlow::BuildEncoder(Encoder *encoder) {
  encoder->forward = AddFunction(encoder->name);
  FlowBuilder tf(this, encoder->forward);
  encoder->embeddings =
      tf.Random(tf.Parameter("embeddings", DT_FLOAT, {encoder->dims, dims}));
  encoder->features =
      tf.Placeholder("features", DT_INT32, {1, encoder->max_features});
  auto *sum = tf.GatherSum(encoder->embeddings, encoder->features);
  if (normalize) {
    auto *length = tf.Name(tf.Norm(sum), "length");
    encoder->encoding = tf.Name(tf.Div(sum, length), "encoding");
  } else {
    encoder->encoding = tf.Name(sum, "encoding");
  }
  encoder->encoding->set_ref();
}

DualEncoderBatch::DualEncoderBatch(const DualEncoderFlow &flow,
                                   const Network  &model,
                                   const CrossEntropyLoss &loss)
    : sim_(model.GetCell(flow.sim)),
      gsim_(model.GetCell(flow.gsim)),
      gleft_(model.GetCell(flow.left.backward)),
      gright_(model.GetCell(flow.right.backward)),
      loss_(loss) {
  // Get cells for left end right encoders.
  const Cell *l = model.GetCell(flow.left.forward);
  const Cell *gl = model.GetCell(flow.left.backward);
  const Cell *r = model.GetCell(flow.right.forward);
  const Cell *gr = model.GetCell(flow.right.backward);

  // Allocate instances for all batch elements.
  elements_.reserve(flow.batch_size);
  for (int i = 0; i < flow.batch_size; ++i) {
    elements_.emplace_back(l, r);
  }

  // Get tensors.
  left_features_ = l->GetParameter(flow.left.features);
  auto *left_encoding = l->GetParameter(flow.left.encoding);
  right_features_ = r->GetParameter(flow.right.features);
  auto *right_encoding = r->GetParameter(flow.right.encoding);

  sim_cosine_ = sim_.cell()->GetParameter(flow.sim_cosine);
  auto *sim_left = sim_.cell()->GetParameter(flow.sim_left);
  auto *sim_right = sim_.cell()->GetParameter(flow.sim_right);

  gleft_primal_ = gl->GetParameter(flow.left.primal);
  gleft_d_encoding_ = gl->GetParameter(flow.left.d_encoding);

  gright_primal_ = gr->GetParameter(flow.right.primal);
  gright_d_encoding_ = gr->GetParameter(flow.right.d_encoding);

  gsim_d_cosine_ = sim_cosine_->Gradient();
  gsim_d_left_ = sim_left->Gradient();
  gsim_d_right_ = sim_right->Gradient();

  // Set up static references between cells.
  for (int i = 0; i < flow.batch_size; ++i) {
    elements_[i].left.SetReference(left_encoding,
                                   sim_.Get<float>(sim_left, i));
    elements_[i].right.SetReference(right_encoding,
                                    sim_.Get<float>(sim_right, i));
  }

  auto *gsim_primal = gsim_.cell()->GetParameter(flow.gsim_primal);
  gsim_.Set(gsim_primal, &sim_);
}

float DualEncoderBatch::Compute() {
  // Get batch size.
  int batch_size = elements_.size();

  // Compute left encodings.
  for (int i = 0; i < batch_size; ++i) {
    elements_[i].left.Compute();
  }

  // Compute right encodings.
  for (int i = 0; i < batch_size; ++i) {
    elements_[i].right.Compute();
  }

  // Compute similarity for all pairs in batch.
  sim_.Compute();

  // Compute loss for all instances in batch and compute the loss gradient.
  // For each row in the similarity matrix we compute a batch softmax
  // cross-entropy loss where the positive instances are in the diagonal and
  // the negative examples are off the diagonal.
  float loss = 0.0;
  for (int i = 0; i < batch_size; ++i) {
    float *logits = sim_.Get<float>(sim_cosine_, i);
    float *glogits = gsim_.Get<float>(gsim_d_cosine_, i);
    loss += loss_.Compute(logits, i, glogits);
  }

  // Propagate gradient through gsim.
  gsim_.Compute();

  // Propagate gradient through left encoder.
  for (int i = 0; i < batch_size; ++i) {
    // Set reference to primal cell.
    gleft_.Set(gleft_primal_, &elements_[i].left);

    // Set reference to gradient from similarity gradient.
    auto *d_encoding = gsim_.Get<float>(gsim_d_left_, i);
    gleft_.SetReference(gleft_d_encoding_, d_encoding);

    gleft_.Compute();
  }

  // Propagate gradient through right encoder.
  for (int i = 0; i < batch_size; ++i) {
    // Set reference to primal cell.
    gright_.Set(gright_primal_, &elements_[i].right);

    // Set reference to gradient from similarity gradient.
    auto *d_encoding = gsim_.Get<float>(gsim_d_right_, i);
    gright_.SetReference(gright_d_encoding_, d_encoding);

    gright_.Compute();
  }

  // Return average loss.
  return loss / batch_size;
}

void DualEncoderBatch::Reset() {
  gleft_.Clear();
  gright_.Clear();
}

}  // namespace nlp
}  // namespace sling

