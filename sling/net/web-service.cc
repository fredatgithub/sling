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

#include "sling/net/web-service.h"

#include "sling/stream/stream.h"
#include "sling/frame/decoder.h"
#include "sling/frame/encoder.h"
#include "sling/frame/json.h"
#include "sling/frame/printer.h"
#include "sling/frame/object.h"
#include "sling/frame/reader.h"
#include "sling/frame/store.h"
#include "sling/net/http-server.h"
#include "sling/stream/memory.h"
#include "sling/string/numbers.h"

namespace sling {

WebService::WebService(Store *commons,
                       HTTPRequest *request,
                       HTTPResponse *response)
    : store_(commons),
      request_(request),
      response_(response),
      query_(request->query()) {
  // Initialize input and output.
  input_ = Frame(&store_, Handle::nil());
  output_ = Frame(&store_, Handle::nil());

  // Determine input format.
  Text content_type = request->content_type();
  if (content_type.empty()) {
    input_format_ = UNKNOWN;
  } else if (content_type == "application/sling") {
    input_format_ = ENCODED;
  } else if (content_type == "text/sling") {
    input_format_ = TEXT;
  } else if (content_type == "application/json") {
    input_format_ = CJSON;
  } else if (content_type == "text/json") {
    input_format_ = JSON;
  } else if (content_type == "text/lex") {
    input_format_ = LEX;
  } else if (content_type == "text/plain") {
    input_format_ = PLAIN;
  }

  // Decode input.
  if (request->content_length() > 0) {
    ArrayInputStream stream(request->content(), request->content_length());
    Input in(&stream);
    switch (input_format_) {
      case ENCODED: {
        // Parse input as encoded SLING frames.
        Decoder decoder(&store_, &in);
        input_ = decoder.DecodeAll();
        break;
      }

      case TEXT:
      case COMPACT: {
        // Parse input as SLING frames in text format.
        Reader reader(&store_, &in);
        input_ = reader.Read();
        break;
      }

      case JSON:
      case CJSON: {
        // Parse input as JSON.
        Reader reader(&store_, &in);
        reader.set_json(true);
        input_ = reader.Read();
        break;
      }

      case LEX:
      case PLAIN: {
        // Get plain text from request body.
        size_t size = request->content_length();
        Handle str = store_.AllocateString(size);
        StringDatum *obj = store_.Deref(str)->AsString();
        if (in.Read(obj->data(), size)) {
          input_ = Object(&store_, str);
        }
        break;
      }

      case EMPTY:
      case UNKNOWN:
        // Ignore empty or unknown input formats.
        break;
    }
  }
}

WebService::~WebService() {
  // Do not generate a response if output is empty or if there is an error.
  if (output_.invalid()) return;
  if (response_->status() != 200) return;

  // Use input format to determine output format if it has not been set.
  if (output_format_ == EMPTY) output_format_ = input_format_;

  // Change output format based on fmt parameter.
  Text fmt = Get("fmt");
  if (!fmt.empty()) {
    if (fmt == "enc") {
      output_format_ = ENCODED;
    } else if (fmt == "txt") {
      output_format_ = TEXT;
    } else if (fmt == "lex") {
      output_format_ = LEX;
    } else if (fmt == "compact") {
      output_format_ = COMPACT;
    } else if (fmt == "json") {
      output_format_ = JSON;
    } else if (fmt == "cjson") {
      output_format_ = CJSON;
    }
  }

  // Fall back to binary encoded SLING format.
  if (output_format_ == EMPTY || output_format_ == UNKNOWN) {
    output_format_ = ENCODED;
  }

  // Output response.
  IOBufferOutputStream stream(response_->buffer());
  Output out(&stream);
  switch (output_format_) {
    case ENCODED: {
      // Output as encoded SLING frames.
      response_->set_content_type("application/sling");
      Encoder encoder(&store_, &out);
      encoder.Encode(output_);
      break;
    }

    case TEXT: {
      // Output as human-readable SLING frames.
      response_->set_content_type("text/sling; charset=utf-8");
      Printer printer(&store_, &out);
      printer.set_indent(2);
      printer.set_byref(byref_);
      printer.Print(output_);
      break;
    }

    case COMPACT: {
      // Output compact SLING text.
      response_->set_content_type("text/sling; charset=utf-8");
      Printer printer(&store_, &out);
      printer.set_byref(byref_);
      printer.Print(output_);
      break;
    }

    case JSON: {
      // Output in JSON format.
      response_->set_content_type("text/json; charset=utf-8");
      JSONWriter writer(&store_, &out);
      writer.set_indent(2);
      writer.set_byref(byref_);
      writer.Write(output_);
      break;
    }

    case CJSON: {
      // Output in compact JSON format.
      response_->set_content_type("application/json; charset=utf-8");
      JSONWriter writer(&store_, &out);
      writer.set_byref(byref_);
      writer.Write(output_);
      break;
    }

    case LEX: {
      // Output is a LEX-encoded string.
      if (!output_.IsString()) {
        response_->SendError(500, "Internal Server Error", "no lex output");
      } else {
        response_->set_content_type("text/lex");
        out.Write(output_.AsString().text());
      }
      break;
    }

    case PLAIN: {
      // Output plain text string.
      if (!output_.IsString()) {
        response_->SendError(500, "Internal Server Error", "no output");
      } else {
        response_->set_content_type("text/plain");
        out.Write(output_.AsString().text());
      }
      break;
    }

    case EMPTY:
    case UNKNOWN:
      // Ignore empty or unknown output formats.
      break;
  }
}

}  // namespace sling

