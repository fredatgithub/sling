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

#include "sling/nlp/parser/parser.h"

#include "sling/frame/serialization.h"

REGISTER_COMPONENT_REGISTRY("parser delegate", sling::nlp::Delegate);

namespace sling {
namespace nlp {

using namespace myelin;

Parser::~Parser() {
  delete encoder_;
  for (auto *d : delegates_) delete d;
}

void Parser::Load(Store *store, const string &model) {
  // Load and compile parser flow.
  Flow flow;
  CHECK(flow.Load(model));
  compiler_.Compile(&flow, &network_);

  // Load commons store from parser model.
  Flow::Blob *commons = flow.DataBlock("commons");
  if (commons != nullptr) {
    StringDecoder decoder(store, commons->data, commons->size);
    decoder.DecodeAll();
  }

  // Get parser specification.
  Flow::Blob *spec_data = flow.DataBlock("parser");
  CHECK(spec_data != nullptr) << "No parser specification in model: " << model;
  StringDecoder spec_decoder(store, spec_data->data, spec_data->size);
  Frame spec = spec_decoder.Decode().AsFrame();
  CHECK(spec.valid());

  // Get parser model hyperparameters.
  Frame hparams = spec.GetFrame("hparams");
  if (hparams.valid()) {
    for (const Slot &p : hparams) {
      string name = String(store, p.name).value();
      string value = String(store, p.value).value();
      hparams_.emplace_back(name, value);
    }
  }

  // Initialize encoder.
  Frame encoder_spec = spec.GetFrame("encoder");
  CHECK(encoder_spec.valid());
  string encoder_type = encoder_spec.GetString("type");
  encoder_ = Encoder::Create(encoder_type);
  encoder_->Load(&flow, encoder_spec);
  encoder_->Initialize(network_);

  // Initialize decoder.
  Frame decoder_spec = spec.GetFrame("decoder");
  CHECK(decoder_spec.valid());
  CHECK_EQ(decoder_spec.GetText("type"), "transition");
  int frame_limit = decoder_spec.GetInt("frame_limit");
  skip_section_titles_ = decoder_spec.GetBool("skip_section_titles", true);

  // Initialize roles.
  Array roles = decoder_spec.Get("roles").AsArray();
  if (roles.valid()) {
    for (int i = 0; i < roles.length(); ++i) {
      roles_.Add(roles.get(i));
    }
  }

  // Initialize decoder cascade.
  Array delegates = decoder_spec.Get("delegates").AsArray();
  CHECK(delegates.valid());
  for (int i = 0; i < delegates.length(); ++i) {
    Frame delegate_spec(store, delegates.get(i));
    string type = delegate_spec.GetString("type");
    Delegate *delegate = Delegate::Create(type);
    delegate->Initialize(network_, delegate_spec);
    delegates_.push_back(delegate);
  }

  // Initialize decoder feature model.
  decoder_ = network_.GetCell("decoder");
  feature_model_.Init(decoder_, &roles_, frame_limit);
}

void Parser::Parse(Document *document) const {
  // Create encoder and delegate instances.
  std::vector<DelegateInstance *> delegates;
  for (auto *d : delegates_) delegates.push_back(d->CreateInstance());
  EncoderInstance *encoder = encoder_->CreateInstance();

  // Parse each sentence of the document.
  for (SentenceIterator s(document); s.more(); s.next()) {
    // Skip section titles if requested.
    if (skip_section_titles_) {
      const Token &first = document->token(s.begin());
      if (first.style() & HEADING_BEGIN) continue;
    }

    // Get encodings for tokens in the sentence.
    Channel *encodings = encoder->Compute(*document, s.begin(), s.end());

    // Initialize decoder.
    ParserState state(document, s.begin(), s.end());
    ParserFeatureExtractor features(&feature_model_, &state);
    Instance decoder(decoder_);
    Channel activations(feature_model_.activation());

    // Run decoder to predict transitions.
    while (!state.done()) {
      // Allocate space for next step.
      activations.push();

      // Attach instance to recurrent layers.
      decoder.Clear();
      features.Attach(encodings, &activations, &decoder);

      // Extract features.
      features.Extract(&decoder);

      // Compute decoder activations.
      decoder.Compute();

      // Run the cascade.
      ParserAction action(ParserAction::CASCADE, 0);
      int step = state.step();
      float *activation = reinterpret_cast<float *>(activations.at(step));
      int d = 0;
      for (;;) {
        delegates[d]->Predict(activation, &action);
        if (action.type != ParserAction::CASCADE) break;
        CHECK_GT(action.delegate, d);
        d = action.delegate;
      }

      // Fall back to SHIFT if predicted action is not valid.
      if (!state.CanApply(action)) {
        action.type = ParserAction::SHIFT;
      }

      // Apply action to parser state.
      state.Apply(action);
    }
  }

  delete encoder;
  for (auto *d : delegates) delete d;
}

}  // namespace nlp
}  // namespace sling

