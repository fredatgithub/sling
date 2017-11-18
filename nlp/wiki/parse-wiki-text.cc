#include <iostream>
#include <string>

#include "base/flags.h"
#include "base/init.h"
#include "base/logging.h"
#include "base/types.h"
#include "file/file.h"
#include "nlp/wiki/wiki-parser.h"

DEFINE_string(input, "test.txt", "input file with wiki text");

using namespace sling;
using namespace sling::nlp;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  string wikitext;
  CHECK(File::ReadContents(FLAGS_input, &wikitext));

  WikiParser parser(wikitext.c_str());
  parser.Parse();
  parser.Extract();

  std::cout << "<html>\n";
  std::cout << "<head>\n";
  std::cout << "<meta charset='utf-8'/>\n";
  std::cout << "</head>\n";
  std::cout << "<body>\n";
  std::cout <<  parser.text() << "\n";
  std::cout << "<h1>AST</h1>\n<pre>\n";
  parser.PrintAST(0, 0);
  std::cout << "</pre>\n";
  std::cout << "</body></html>\n";

  return 0;
}

