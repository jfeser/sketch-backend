#include "InputReader.h"
#include "BooleanDAG.h"



#define YYERROR_VERBOSE 1

#include <string>
#include <sstream>
#include <stack>

#include <list>

using namespace std;

namespace INp{

int isatty(int i);
  map<string, BooleanDAG*> functionMap;
  map<string, BooleanDAG*> sketchMap;
  map<BooleanDAG*, string> sketches;
  int global_filterid = 0;

#include "InputLexer.c"
#include "InputParser.c"

}

