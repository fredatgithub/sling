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

#include "sling/nlp/ner/measures.h"

#include "sling/frame/object.h"
#include "sling/nlp/kb/calendar.h"

namespace sling {
namespace nlp {

// Measures:
//  number (float/integer) and compound numbers (e.g. 15 mio)
//  date plus stand-alone years (1000-2100), month and year, stand-alone month,
//  and weekdays
//  quantity with unit
//  amount with currency
//  entities (person, location, organization, facility)
//
// Add all anchors from input document that matches in the phrase tables and
// add the correct resolution as the aux item.
//
// For persons, add last name mentions as resolved mentions
// Aux items take precendence over the matches
//
// Absolute calendar types:
//   millennium (Q36507)
//   century (Q578)
//   decade (Q39911)
//   year (Q577)
//   calendar day of a given year (Q47150325) (e.g. 3 February 2015)
//   calendar month of a given year (Q47018478) (e.g February 2015)
//
// Relative calendar types:
//   calendar month (Q47018901) (January, February, ...)
//   determinator for date of periodic occurrence (Q14795564) (e.g. February 3)
//   day of the week (Q41825) (Monday, Tueday, ...)
//   day of the week within a given month (Q51118183)
//
// integer number between 1592 and 2038 is year if it is only digits

void SpanAnnotator::Init(Store *store) {
  CHECK(names_.Bind(store));
}

Handle SpanAnnotator::FindMatch(const PhraseTable &aliases,
                                const PhraseTable::Phrase *phrase,
                                const Name &type,
                                Store *store) {
  Handles matches(store);
  aliases.GetMatches(phrase, &matches);
  for (Handle h : matches) {
    Frame item(store, h);
    for (const Slot &s : item) {
      if (s.name == n_instance_of_ && store->Resolve(s.value) == type) {
        return h;
      }
    }
  }
  return Handle::nil();
}

void SpanImporter::Annotate(SpanChart *chart) {
  int begin = chart->begin();
  int end = chart->end();
  for (int i = 0; i < chart->document()->num_spans(); ++i) {
    Span *span = chart->document()->span(i);
    if (span->begin() < begin || span->end() > end) continue;
    Frame evoked = span->Evoked();
    if (evoked.invalid()) continue;
    int flags = 0;
    if (evoked.IsA(n_time_)) flags |= SPAN_DATE;
    if (evoked.IsA(n_quantity_)) flags |= SPAN_MEASURE;
    if (evoked.IsA(n_geo_)) flags |= SPAN_GEO;
    chart->Add(span->begin(), span->end(), evoked.handle(), flags);
  }
}

SpanTaxonomy::~SpanTaxonomy() {
  delete taxonomy_;
}

void SpanTaxonomy::Init(Store *store) {
  static std::pair<const char *, int> span_taxonomy[] = {
    {"Q47150325",  SPAN_CALENDAR_DAY},     // calendar day of a given year
    {"Q47018478",  SPAN_CALENDAR_MONTH},   // calendar month of a given year
    {"Q14795564",  SPAN_DAY_OF_YEAR},      // date of periodic occurrence
    {"Q41825",     SPAN_WEEKDAY},          // day of the week
    {"Q47018901",  SPAN_MONTH},            // calendar month
    {"Q577",       SPAN_YEAR},             // year
    {"Q29964144",  SPAN_YEAR_BC},          // year BC
    {"Q39911",     SPAN_DECADE},           // decade
    {"Q578",       SPAN_CENTURY},          // century
    {"Q21199",     SPAN_NATURAL_NUMBER},   // natural number
    {"Q8142",      SPAN_CURRENCY},         // currency
    {"Q47574",     SPAN_UNIT},             // unit of measurement

    {"Q101352",    SPAN_FAMILY_NAME},      // family name
    {"Q202444",    SPAN_GIVEN_NAME},       // given name

    {"Q215627",    SPAN_PERSON},           // person
    {"Q17334923",  SPAN_LOCATION},         // location
    {"Q43229",     SPAN_ORGANIZATION},     // organization

    //{"Q4164871",   -1},                    // position
    //{"Q180684",    -1},                    // conflict
    //{"Q1656682",   -1},                    // event
    {"Q838948",    -1},                    // work of art

    //{"Q2188189",  -1},          // musical work
    //{"Q571",      -1},              // book
    //{"Q732577",   -1},              // publication
    //{"Q11424",    -1},              // film
    //{"Q15416",    -1},              // television program
    //{"Q47461344", -1},              // written work

    //{"Q35120",    0},                     // entity

    {nullptr, 0},
  };

  SpanAnnotator::Init(store);
  std::vector<Text> types;
  for (auto *type = span_taxonomy; type->first != nullptr; ++type) {
    const char *name = type->first;
    int flags = type->second;
    Handle t = store->LookupExisting(name);
    if (t.IsNil()) {
      LOG(WARNING) << "Ignoring unknown type in taxonomy: " << name;
      continue;
    }
    type_flags_[t] = flags;
    types.push_back(name);
  }

  catalog_.Init(store);
  taxonomy_ = new Taxonomy(&catalog_, types);
}

void SpanTaxonomy::Annotate(const PhraseTable &aliases, SpanChart *chart) {
  Document *document = chart->document();
  Store *store = document->store();
  PhraseTable::MatchList matchlist;
  Handles matches(store);
  for (int b = 0; b < chart->size(); ++b) {
    int end = std::min(b + chart->maxlen(), chart->size());
    for (int e = b + 1; e <= end; ++e) {
      SpanChart::Item &span = chart->item(b, e);

      // Only keep existing annotation for span if the phrase is an alias for
      // the entity, or if it is a date or measure annotation.
      if (!span.aux.IsNil()) {
        if (span.flags & (SPAN_DATE | SPAN_MEASURE | SPAN_GEO)) {
          span.matches = nullptr;
        } else {
          // Check that phase is an alias for the annotation.
          aliases.GetMatches(span.matches, &matches);
          bool found = false;
          for (Handle h : matches) {
            if (h == span.aux) found = true;
          }
          if (found) {
            // Clear other matches.
            span.matches = nullptr;

            // Classify item.
            Frame item(store, span.aux);
            Handle type = taxonomy_->Classify(item);
            if (!type.IsNil()) {
              auto f = type_flags_.find(type);
              if (f != type_flags_.end()) {
                if (f->second == -1) {
                  span.aux = Handle::nil();
                } else {
                  span.flags |= f->second;
                }
              }
            }
          } else {
            // Span is not an alias for the annotated entity.
            span.aux = Handle::nil();
          }
        }
      }

      if (span.aux.IsNil() && span.matches != nullptr) {
        aliases.GetMatches(span.matches, &matchlist);
        CaseForm form = document->Form(b + chart->begin(), e + chart->begin());
        bool nomatch = true;
        bool only_discard = true;
        for (const auto &match : matchlist) {
          // Skip if case forms conflict.
          if (match.form != CASE_NONE &&
              form != CASE_NONE &&
              match.form != form) {
            continue;
          }
          nomatch = false;

          // Classify item.
          Frame item(store, match.item);
          Handle type = taxonomy_->Classify(item);
          if (type.IsNil()) {
            only_discard = false;
            continue;
          }

          auto f = type_flags_.find(type);
          if (f == type_flags_.end()) {
            only_discard = false;
            continue;
          }

          if (f->second == -1) continue;

          span.flags |= f->second;
          only_discard = false;

          //Frame t(store, type);
          //LOG(INFO) << "'" << chart->phrase(b, e) << "': " << item.Id()
          //          << " " << item.GetString("name") << " is "
          //          << t.GetString("name") << " reliable: " << match.reliable;
        }

        // Remove matches if all matches have conflicting case forms.
        if (nomatch) {
          //LOG(INFO) << "No match '" << chart->phrase(b, e) << "'";
          span.matches = nullptr;
        } else if (only_discard) {
          //LOG(INFO) << "Discard '" << chart->phrase(b, e) << "'";
          span.matches = nullptr;
        }
      }
    }
  }
}

void NumberAnnotator::Annotate(SpanChart *chart) {
  // Get document language.
  Document *document = chart->document();
  Handle lang = document->top().GetHandle(n_lang_);
  if (lang.IsNil()) lang = n_english_.handle();
  Format format = (lang == n_english_ ? IMPERIAL : STANDARD);

  for (int t = chart->begin(); t < chart->end(); ++t) {
    const string &word = document->token(t).word();

    // Check if token contains digits.
    bool has_digits = false;
    bool all_digits = true;
    for (char c : word) {
      if (c >= '0' && c <= '9') {
        has_digits = true;
      } else {
        all_digits = true;
      }
    }

    // Try to parse token as a number.
    if (has_digits) {
      Handle number = ParseNumber(word, format);
      if (!number.IsNil()) {
        // Numbers between 1582 and 2038 are considered years.
        int flags = SPAN_NUMBER;
        if (word.size() == 4 && all_digits && number.IsInt()) {
          int value = number.AsInt();
          if (value >= 1582 && value <= 2038) {
            Builder b(document->store());
            b.AddIsA(n_time_);
            b.AddIs(number);
            number = b.Create().handle();
            flags = SPAN_DATE;
          }
        }
        chart->Add(t, t + 1, number, flags);
      }
    }
  }
}

Handle NumberAnnotator::ParseNumber(Text str, char tsep, char dsep, char msep) {
  const char *p = str.data();
  const char *end = p + str.size();
  if (p == end) return Handle::nil();

  // Parse sign.
  double scale = 1.0;
  if (*p == '-') {
    scale = -1.0;
    p++;
  } else if (*p == '+') {
    p++;
  }

  // Parse integer part.
  double value = 0.0;
  const char *group = nullptr;
  while (p < end) {
    if (*p >= '0' && *p <= '9') {
      value = value * 10.0 + (*p++ - '0');
    } else if (*p == tsep) {
      if (group != nullptr && p - group != 3) return Handle::nil();
      group = p + 1;
      p++;
    } else if (*p == dsep) {
      break;
    } else {
      return Handle::nil();
    }
  }
  if (group != nullptr && p - group != 3) return Handle::nil();

  // Parse decimal part.
  bool decimal = false;
  if (p < end && *p == dsep) {
    decimal = true;
    p++;
    group = nullptr;
    while (p < end) {
      if (*p >= '0' && *p <= '9') {
        value = value * 10.0 + (*p++ - '0');
        scale /= 10.0;
      } else if (*p == msep) {
        if (group != nullptr && p - group != 3) return Handle::nil();
        group = p + 1;
        p++;
      } else {
        return Handle::nil();
      }
    }
    if (group != nullptr && p - group != 3) return Handle::nil();
  }
  if (p != end) return Handle::nil();

  // Compute number.
  value *= scale;
  if (decimal || value < Handle::kMinInt || value > Handle::kMaxInt) {
    return Handle::Float(value);
  } else {
    return Handle::Integer(value);
  }
}

Handle NumberAnnotator::ParseNumber(Text str, Format format) {
  Handle number = Handle::nil();
  switch (format) {
    case STANDARD:
      number = ParseNumber(str, '.', ',', 0);
      if (number.IsNil()) number = ParseNumber(str, ',', '.', 0);
      break;
    case IMPERIAL:
      number = ParseNumber(str, ',', '.', 0);
      if (number.IsNil()) number = ParseNumber(str, '.', ',', 0);
      break;
    case NORWEGIAN:
      number = ParseNumber(str, ' ', '.', ' ');
      if (number.IsNil()) number = ParseNumber(str, '.', ',', 0);
      break;
  }
  return number;
}

void NumberScaleAnnotator::Init(Store *store) {
  static std::pair<const char *, float> scalars[] = {
    {"Q43016", 1e3},      // thousand
    {"Q38526", 1e6},      // million
    {"Q16021", 1e9},      // billion
    {"Q862978", 1e12},    // trillion
    {nullptr, 0},
  };

  for (auto *scale = scalars; scale->first != nullptr; ++scale) {
    const char *qid = scale->first;
    float scalar = scale->second;
    scalars_[store->LookupExisting(qid)] = scalar;
  }
}

void NumberScaleAnnotator::Annotate(const PhraseTable &aliases,
                                    SpanChart *chart) {
  Document *document = chart->document();
  Store *store = document->store();
  for (int b = 0; b < chart->size(); ++b) {
    int end = std::min(b + chart->maxlen(), chart->size());
    for (int e = end; e > b; --e) {
      SpanChart::Item &span = chart->item(b, e);
      if (!span.is(SPAN_NATURAL_NUMBER)) continue;
      if (span.is(SPAN_NUMBER)) continue;

      // Get scalar.
      float scale = 0;
      Handles matches(store);
      aliases.GetMatches(span.matches, &matches);
      for (Handle item : matches) {
        auto f = scalars_.find(item);
        if (f != scalars_.end()) {
          scale = f->second;
          break;
        }
      }
      if (scale == 0) continue;

      // Find number to the left.
      int left_end = b;
      int left_begin = std::max(0, left_end - chart->maxlen());
      Handle number = Handle::nil();
      int start;
      for (int left = left_begin; left < left_end; ++left) {
        SpanChart::Item &number_span = chart->item(left, left_end);
        if (!number_span.is(SPAN_NUMBER)) continue;
        if (number_span.aux.IsNumber()) {
          start = left;
          number = number_span.aux;
          break;
        }
      }

      // Add scaled number annotation.
      if (number.IsInt()) {
        float value = number.AsInt() * scale;
        chart->Add(start + chart->begin(), e + chart->begin(),
                   Handle::Float(value), SPAN_NUMBER);
      } else if (number.IsFloat()) {
        float value = number.AsFloat() * scale;
        chart->Add(start + chart->begin(), e + chart->begin(),
                   Handle::Float(value), SPAN_NUMBER);
      }
    }
  }
}

void MeasureAnnotator::Init(Store *store) {
  static const char *unit_types[] = {
    "Q10387685",   // unit of density
    "Q10387689",   // unit of power
    "Q1302471",    // unit of volume
    "Q1371562",    // unit of area
    "Q15222637",   // unit of speed
    "Q15976022",   // unit of force
    "Q16604158",   // unit of charge
    "Q1790144",    // unit of time
    "Q1978718",    // unit of length
    "Q2916980",    // unit of energy
    "Q3647172",    // unit of mass
    "Q8142",       // currency
    "Q756202",     // reserve currency
    nullptr,
  };

  SpanAnnotator::Init(store);
  for (const char **type = unit_types; *type != nullptr; ++type) {
    units_.insert(store->Lookup(*type));
  }
}

void MeasureAnnotator::Annotate(const PhraseTable &aliases, SpanChart *chart) {
  Document *document = chart->document();
  Store *store = document->store();
  PhraseTable::MatchList matches;
  for (int b = 0; b < chart->size(); ++b) {
    int end = std::min(b + chart->maxlen(), chart->size());
    for (int e = end; e > b; --e) {
      SpanChart::Item &span = chart->item(b, e);
      if (!span.is(SPAN_UNIT) && !span.is(SPAN_CURRENCY)) continue;

      // Get unit.
      Handle unit = Handle::nil();
      PhraseTable::MatchList matches;
      aliases.GetMatches(span.matches, &matches);
      for (auto &match : matches) {
        if (!match.reliable) continue;
        Frame item(store, match.item);
        for (const Slot &s : item) {
          if (s.name == n_instance_of_) {
            Handle type = store->Resolve(s.value);
            if (units_.count(type) > 0) {
              unit = match.item;
              break;
            }
          }
        }
        if (!unit.IsNil()) break;
      }
      if (unit.IsNil()) continue;

      // Find number to the left.
      int left_end = b;
      if (left_end > 0 && chart->token(left_end - 1).word() == "-") {
        // Allow dash between number and unit.
        left_end--;
      }
      int left_begin = std::max(0, left_end - chart->maxlen());
      Handle number = Handle::nil();
      int start;
      for (int left = left_begin; left < left_end; ++left) {
        SpanChart::Item &number_span = chart->item(left, left_end);
        if (!number_span.is(SPAN_NUMBER)) continue;
        if (number_span.aux.IsNumber()) {
          start = left;
          number = number_span.aux;
          break;
        }
      }

      // Add quantity annotation.
      if (!number.IsNil()) {
        AddQuantity(chart, start, e, number, unit);
        break;
      }

      // Find number to the right for currency (e.g. USD 100).
      if (span.is(SPAN_CURRENCY)) {
        int right_begin = e;
        int right_end = std::min(right_begin + chart->maxlen(), chart->size());
        Handle number = Handle::nil();
        int end;
        for (int right = right_end - 1; right >= right_begin; --right) {
          SpanChart::Item &number_span = chart->item(right_begin, right);
          if (!number_span.is(SPAN_NUMBER)) continue;
          if (number_span.aux.IsNumber()) {
            end = right;
            number = number_span.aux;
            break;
          }
        }

        // Add quantity annotation for amount.
        if (!number.IsNil()) {
          AddQuantity(chart, b, end, number, unit);
        }
      }
    }
  }
}

void MeasureAnnotator::AddQuantity(SpanChart *chart, int begin, int end,
                                   Handle amount, Handle unit) {
  Store *store = chart->document()->store();
  Builder builder(store);
  builder.AddIsA(n_quantity_);
  builder.Add(n_amount_, amount);
  builder.Add(n_unit_, unit);
  Handle h = builder.Create().handle();
  chart->Add(begin + chart->begin(), end + chart->begin(), h, SPAN_MEASURE);
}

void DateAnnotator::Init(Store *store) {
  SpanAnnotator::Init(store);
  calendar_.Init(store);
}

void DateAnnotator::AddDate(SpanChart *chart, int begin, int end,
                            const Date &date) {
  Store *store = chart->document()->store();
  Builder builder(store);
  builder.AddIsA(n_time_);
  builder.AddIs(date.AsHandle(store));
  Handle h = builder.Create().handle();
  chart->Add(begin + chart->begin(), end + chart->begin(), h, SPAN_DATE);
}

int DateAnnotator::GetYear(const PhraseTable &aliases,
                           Store *store, SpanChart *chart,
                           int pos, int *end) {
  // Skip date delimiters.
  if (pos == chart->size()) return 0;
  const string &word = chart->token(pos).word();
  if (word == "," || word == "de" || word == "del") pos++;

  // Try to find year annotation at position.
  for (int e = std::min(pos + chart->maxlen(), chart->size()); e > pos; --e) {
    SpanChart::Item &span = chart->item(pos, e);
    Handle year = Handle::nil();
    if (span.is(SPAN_YEAR)) {
      year = FindMatch(aliases, span.matches, n_year_, store);
    } else if (span.is(SPAN_YEAR_BC)) {
      year = FindMatch(aliases, span.matches, n_year_bc_, store);
    }
    if (!year.IsNil()) {
      Date date(Object(store, year));
      if (date.precision == Date::YEAR) {
        *end = e;
        return date.year;
      }
    }
  }
  return 0;
}

void DateAnnotator::Annotate(const PhraseTable &aliases, SpanChart *chart) {
  Document *document = chart->document();
  Store *store = document->store();
  PhraseTable::MatchList matches;
  for (int b = 0; b < chart->size(); ++b) {
    int end = std::min(b + chart->maxlen(), chart->size());
    for (int e = end; e > b; --e) {
      SpanChart::Item &span = chart->item(b, e);
      Date date;
      if (span.is(SPAN_CALENDAR_DAY)) {
        // Date with year, month and day.
        Handle h = FindMatch(aliases, span.matches, n_calendar_day_, store);
        if (!h.IsNil()) {
          Frame item(store, h);
          date.ParseFromFrame(item);
          if (date.precision == Date::DAY) {
            AddDate(chart, b, e, date);
            b = e;
            break;
          }
        }
      } else if (span.is(SPAN_CALENDAR_MONTH)) {
        // Date with month and year.
        Handle h = FindMatch(aliases, span.matches, n_calendar_month_, store);
        if (!h.IsNil()) {
          Frame item(store, h);
          date.ParseFromFrame(item);
          if (date.precision == Date::MONTH) {
            AddDate(chart, b, e, date);
            b = e;
            break;
          }
        }
      } else if (span.is(SPAN_DAY_OF_YEAR)) {
        // Day of year with day and month.
        Handle h = FindMatch(aliases, span.matches, n_day_of_year_, store);
        if (calendar_.GetDayAndMonth(h, &date)) {
          int year = GetYear(aliases, store, chart, e, &e);
          if (year != 0) {
            date.year = year;
            date.precision = Date::DAY;
            AddDate(chart, b, e, date);
            b = e;
            break;
          }
        }
      } else if (span.is(SPAN_CALENDAR_MONTH)) {
        // Month.
        Handle h = FindMatch(aliases, span.matches, n_month_, store);
        if (calendar_.GetMonth(h, &date)) {
          int year = GetYear(aliases, store, chart, e, &e);
          if (year != 0) {
            date.year = year;
            date.precision = Date::MONTH;
            AddDate(chart, b, e, date);
            b = e;
            break;
          }
        }
        break;
      } else if (span.is(SPAN_YEAR) && !span.is(SPAN_NUMBER)) {
        Handle h = FindMatch(aliases, span.matches, n_year_, store);
        date.ParseFromFrame(Frame(store, h));
        if (date.precision == Date::YEAR) {
          AddDate(chart, b, e, date);
          b = e;
          break;
        }
      } else if (span.is(SPAN_DECADE)) {
        Handle h = FindMatch(aliases, span.matches, n_decade_, store);
        date.ParseFromFrame(Frame(store, h));
        if (date.precision == Date::DECADE) {
          AddDate(chart, b, e, date);
          b = e;
          break;
        }
      } else if (span.is(SPAN_CENTURY)) {
        Handle h = FindMatch(aliases, span.matches, n_century_, store);
        date.ParseFromFrame(Frame(store, h));
        if (date.precision == Date::CENTURY) {
          AddDate(chart, b, e, date);
          b = e;
          break;
        }
      }
    }
  }
}

}  // namespace nlp
}  // namespace sling
