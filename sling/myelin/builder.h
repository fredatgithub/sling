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

#ifndef SLING_MYELIN_BUILDER_H_
#define SLING_MYELIN_BUILDER_H_

#include <map>
#include <vector>

#include "sling/base/types.h"
#include "sling/myelin/flow.h"

namespace sling {
namespace myelin {

// A score is used for defined a name space for variables and operations.
class Scope {
 public:
  Scope(Scope *parent, const string &name);
  ~Scope();

  // Return unique name for operation.
  string OpName(const string &op);

  // Return scope name prefix.
  const string prefix() const { return name_; }

 private:
  Scope *root_;     // root scope
  Scope *parent_;   // parent scope of this scope
  Scope *current_;  // current inner-most scope for root scope
  string name_;     // name prefix for scope

  // Next unused operation number for each operation type.
  std::map<string, int> opnum_;
};

// Flow builder utility for building flows from expressions, e.g.:
//   Flow flow;
//   Builder tf(&flow, "mnist");
//   auto *w = tf.Constant(weights, DT_FLOAT, {784, 10});
//   auto *b = tf.Constant(bias, DT_FLOAT, {10});
//   auto *x = tf.Var("x", DT_FLOAT, {1, 784});
//   auto *y = tf.Add(tf.MatMul(x, w), b);
class Builder : public Scope {
 public:
  // Flow typedefs.
  typedef Flow::Variable Variable;
  typedef Flow::Operation Operation;
  typedef Flow::Function Function;

  // Initialize builder for existing function.
  Builder(Flow *flow, Function *func)
      : Scope(nullptr, func->name),
        flow_(flow),
        func_(func) {}

  // Initialize builder for new function.
  Builder(Flow *flow, const string &name) : Scope(nullptr, name), flow_(flow) {
    func_ = flow->AddFunction(name);
  }

  // Add variable to flow.
  Variable *Var(const string &name, Type type, const Shape &shape);

  // Add learnable parameter variable to flow.
  Variable *Parameter(const string &name, Type type, const Shape &shape);

  // Add input variable to function.
  Variable *Placeholder(const string &name, Type type, const Shape &shape);

  // Change name of variable. Returns the variable itself.
  Variable *Name(Variable *var, const string &name);

  // Add operation to function and return output variable.
  Variable *Op(const string &op,
               const std::vector<Variable *> &args,
               Type type,
               const Shape &shape);

  // Add operation to function and return output variable. The output is
  // shaped using broadcast semantics.
  Variable *Op(const string &op, const std::vector<Variable *> &args);

  // Add operation with no output to function.
  void Op0(const string &op, const std::vector<Variable *> &args);

  // Add constant to flow.
  Variable *Const(const void *data, Type type, const Shape &shape);
  Variable *Const(float value) { return Const(&value, DT_FLOAT, {}); }
  Variable *Const(int value) { return Const(&value, DT_INT32, {}); }
  Variable *Const(std::vector<float> &value) {
    int size = value.size();
    return Const(value.data(), DT_FLOAT, {size});
  }
  Variable *Const(std::vector<int> &value) {
    int size = value.size();
    return Const(value.data(), DT_INT32, {size});
  }
  Variable *Const(const std::vector<int> &value) {
    int size = value.size();
    return Const(value.data(), DT_INT32, {size});
  }

  // Add instance reference to other function.
  Variable *Instance(Function *func);

  // Add reference to variable in external instance.
  Variable *Ref(Variable *instance, Variable *external);

  // Builder methods for common operations.
  Variable *Add(Variable *x, Variable *y) { return Op("Add", {x, y}); }
  Variable *Sub(Variable *x, Variable *y) { return Op("Sub", {x, y}); }
  Variable *Mul(Variable *x, Variable *y) { return Op("Mul", {x, y}); }
  Variable *Div(Variable *x, Variable *y) { return Op("Div", {x, y}); }
  Variable *Min(Variable *x, Variable *y) { return Op("Minimum", {x, y}); }
  Variable *Max(Variable *x, Variable *y) { return Op("Maximum", {x, y}); }
  Variable *Neg(Variable *x) { return Op("Neg", {x}); }
  Variable *Square(Variable *x) { return Op("Square", {x}); }
  Variable *Reciprocal(Variable *x) { return Op("Reciprocal", {x}); }
  Variable *Abs(Variable *x) { return Op("Abs", {x}); }
  Variable *Log(Variable *x) { return Op("Log", {x}); }
  Variable *Exp(Variable *x) { return Op("Exp", {x}); }
  Variable *Tanh(Variable *x) { return Op("Tanh", {x}); }
  Variable *Sigmoid(Variable *x) { return Op("Sigmoid", {x}); }
  Variable *Relu(Variable *x) { return Op("Relu", {x}); }
  Variable *Identity(Variable *x) { return Op("Identity", {x}); }
  Variable *Cos(Variable *x) { return Op("Cos", {x}); }
  Variable *Sin(Variable *x) { return Op("Sin", {x}); }

  Variable *MatMul(Variable *x, Variable *y);

  Variable *Reshape(Variable *x, Variable *shape) {
    return Op("Reshape", {x, shape});
  }
  Variable *Reshape(Variable *x, const Shape &shape) {
    return Op("Reshape", {x, Const(shape.dims())}, x->type, shape);
  }

  Variable *Transpose(Variable *x) {
    return Op("Transpose", {x}, x->type, x->shape.transpose());
  }

  Variable *Dot(Variable *x, Variable *y, int size) {
    return MatMul(Reshape(x, {1, size}), Reshape(y, {size, 1}));
  }

  Variable *Gather(Variable *M, Variable *f) {
    return Op("Gather", {M, f}, M->type, {f->dim(1), M->dim(1)});
  }
  Variable *GatherSum(Variable *M, Variable *f) {
    return Op("GatherSum", {M, f}, M->type, {f->dim(0)});
  }
  Variable *GatherAvg(Variable *M, Variable *f) {
    return Op("GatherAvg", {M, f}, M->type, {f->dim(0)});
  }
  Variable *GatherMax(Variable *M, Variable *f) {
    return Op("GatherMax", {M, f}, M->type, {f->dim(0)});
  }

  Variable *Scatter(Variable *v, Variable *f, int size) {
    return Op("Scatter", {v, f}, v->type, {size, v->dim(1)});
  }

  void AssignAdd(Variable *var, Variable *value) {
    Op0("AssignAdd", {var, value});
  }

  void ScatterAdd(Variable *M, Variable *f, Variable *v) {
    return Op0("ScatterAdd", {M, f, v});
  }

  // Feed-forward (FF) layer(s).
  Variable *FFLayers(Variable *input,
                     std::vector<int> layers,
                     int hidden = -1,
                     bool bias = false,
                     const string &activation = "Relu");
  Variable *FFLayer(Variable *input, int size, bool bias = false) {
    return FFLayers(input, {size}, -1, bias);
  }

  // Long short-term memory (LSTM) layer.
  Variable *LSTMLayer(Variable *input, int size);

  // Return function for builder.
  Function *func() const { return func_; }

  // Return flow for builder.
  Flow *flow() const { return flow_; }

 private:
  // Flow for builder.
  Flow *flow_;

  // Function for builder.
  Function *func_;
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_BUILDER_H_

