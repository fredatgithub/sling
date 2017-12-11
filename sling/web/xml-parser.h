#ifndef SLING_WEB_XML_PARSER_H_
#define SLING_WEB_XML_PARSER_H_

#include <vector>

#include "sling/stream/input.h"

namespace sling {

struct XMLAttribute {
  XMLAttribute(char *n, char *v) : name(n), value(v) {}
  char *name;
  char *value;
};

struct XMLElement {
  char *name;
  std::vector<XMLAttribute> attrs;

  const char *Get(const char *name, const char *defval) const;
  const char *Get(const char *name) const;
  void Clear() {
    name = nullptr;
    attrs.clear();
  }
};

class XMLParser {
 public:
  XMLParser();
  virtual ~XMLParser();

  // Parse XML from input and call callbacks.
  virtual bool Parse(Input *input);

  // Callbacks.
  virtual bool StartDocument();
  virtual bool EndDocument();
  virtual bool StartElement(const XMLElement &element);
  virtual bool EndElement(const char *name);
  virtual bool Text(const char *str);
  virtual bool Comment(const char *str);
  virtual bool CData(const char *str);
  virtual bool ProcessingInstruction(const XMLElement &element);

  // Input stream.
  Input *input() const { return input_; }

 protected:
  // Initialize document state.
  void Init(Input *input);

  // Add data to buffer.
  void Add(char ch);
  void AddText(char ch);
  void AddString(const char *text);

  // Read next character from input. Return -1 on end of input.
  int ReadChar();

  // Skip whitespace.
  int SkipWhitespace(int ch);

  // Log error.
  bool Error(const char *message);

  // Check if character is an XML name character.
  static bool IsNameChar(int ch);

  // Input with XML text.
  Input *input_ = nullptr;

  // Buffer for elements, attributes, and data.
  char *buffer_;
  char *bufptr_;
  char *bufend_;

  // Pointer to current text string.
  char *txtptr_ = nullptr;

  // Line number.
  int line_ = 1;

  // Current XML element.
  XMLElement element_;

  // Element name stack.
  std::vector<char *> stack_;
};

}  // namespace sling

#endif  // SLING_WEB_XML_PARSER_H_
