#include "BasicError.h"
#include "BooleanDAG.h"
#include "InputReader.h"
#include "SolveFromInput.h"

#include <fstream>
#include <ctime>

using std::ofstream;

namespace INp{
extern  map<string, BooleanDAG*> functionMap;
extern  map<string, BooleanDAG*> sketchMap;
extern  map<BooleanDAG*, string> sketches;
}

string context;

int main(int argc, char** argv){
  int input_idx = 1;
  int seedsize = 1;
  
  for(int ii=0; ii<argc; ++ii){
    if( string(argv[ii]) == "-seedsize" ){
      Assert(ii<(argc-1), "-ws needs an extra parameter");
      seedsize = atoi(argv[ii+1]);
      input_idx = ii+2;      
    }   
  }
  
  
  try{

    Assert( argc > 1, "The input file name must be passed as an argument");
  
    cout<<"Reading Streamit Program in File "<<argv[input_idx]<<endl;

    INp::yyin = fopen(argv[input_idx], "r");
    INp::Inityylex();
    INp::Inityyparse();

    try{
      if (INp::yyparse() != 0) {
		    cerr<<"\n*** Rejected\n";
		    exit(1);
      }
    }catch(BasicError& be){
      cerr<<"There was an error parsing the input"<<endl<<"Exiting compiler"<<endl;
      exit(1);
    }

  	context = " ";
    {
      string fname(argv[input_idx]);
      int x1 = fname.find_last_of("/");
      int x2 = fname.find_last_of("\\");
      int x3 = fname.find_last_of(".");
  
      x1 = x1>x2? x1: x2;
      x3 = x3 > 0? x3 : fname.size();
      ++x1;
      fname = fname.substr(x1, x3-x1);
      string msg = "There is no filter ";
      msg += fname;
      msg += " in file ";
      msg += argv[input_idx];
      cout<<"XXXXXXXXXXXXXXXXXXXXXXX"<<endl;
      //Assert( INp::functionMap.find(fname) != INp::functionMap.end(),  msg );
		ofstream out(argv[input_idx+1]);
      for(map<BooleanDAG*, string>::iterator it = INp::sketches.begin(); it != INp::sketches.end(); ++it){
      	cout<<"PROCESSING SKETCH "<<it->second<<endl;
      	// Dout(INp::functionMap[it->second]->print(cout));
      	// Dout(it->first->print(cout));
      	SolveFromInput solver(INp::functionMap[it->second], it->first, seedsize);
	  	solver.setup();
	  	if( solver.solve() ){
			solver.output_control_map(out);
	  	}else{
	  		return 1;	
	  	}
      }

    }
	return 0;
    }catch(BasicError& be){
      cerr<<"There was an error parsing the input"<<endl<<"Exiting compiler"<<endl;
      exit(1);
    }
}


