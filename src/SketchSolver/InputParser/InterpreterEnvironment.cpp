#include "InterpreterEnvironment.h"
//#include "ABCSATSolver.h"
#include "InputReader.h"
#include "CallGraphAnalysis.h"
#include "ComplexInliner.h"
#include "DagFunctionToAssertion.h"
#include "InputReader.h" // INp yylex_init, yyparse, etc.



#ifdef CONST
#undef CONST
#endif



class Strudel{
	vector<Tvalue>& vals;
	SATSolver* solver;
public:
	Strudel(vector<Tvalue>& vtv, SATSolver* solv):vals(vtv), solver(solv){
		cout<<"This is strange size="<<vtv.size()<<endl;
	}


int valueForINode(INTER_node* inode, VarStore& values, int& nbits){
	Tvalue& tv = vals[inode->id];
	int retval = tv.eval(solver);
	{ cout<<" input "<<inode->get_name()<<" has value "<< retval <<endl; }
	return retval;
}

	void checker(BooleanDAG* dag, VarStore& values, bool_node::Type type){
		cout<<"Entering ~!"<<endl;
		BooleanDAG* newdag = dag->clone();
		vector<bool_node*> inodeList = newdag->getNodesByType(type);
			
		cout<<" * Specializing problem for "<<(type == bool_node::CTRL? "controls" : "inputs")<<endl; 
		cout<<" * Before specialization: nodes = "<<newdag->size()<<" Ctrls = "<<  inodeList.size() <<endl;	
		{
			DagOptim cse(*newdag);			
			
			BooleanDAG* cl = newdag->clone();
			for(int i=0; i<newdag->size() ; ++i ){ 
				// Get the code for this node.				
				if((*newdag)[i] != NULL){
					if((*newdag)[i]->type == bool_node::CTRL){
						INTER_node* inode = dynamic_cast<INTER_node*>((*newdag)[i]);	
						int nbits;
						int t = valueForINode(inode, values, nbits);
						bool_node * repl=NULL;
						if( nbits ==1 ){
							repl = cse.getCnode( t == 1 );						
						}else{
							repl = cse.getCnode( t);							
						}
						Tvalue& tv = vals[i];
						cout<<"ctrl=";
						tv.print(cout, solver);
						cout<<endl;
						Assert( (*newdag)[inode->id] == inode , "The numbering is wrong!!");
						newdag->replace(inode->id, repl);
					}else{
						bool_node* node = cse.computeOptim((*newdag)[i]);
						Tvalue& tv = vals[i];
						cout<<" old = "<<(*cl)[i]->lprint()<<" new "<<node->lprint()<<"  ";
						tv.print(cout, solver);
						if(node->type == bool_node::CONST && cse.getIval( node )==tv.eval(solver)){
							cout<<" good";
						}else{
							cout<<" bad";
						}
						cout<<endl;
						
						if((*newdag)[i] != node){														
								newdag->replace(i, node);							
						}
					}
				}
			}
		}
	}

};





InterpreterEnvironment::~InterpreterEnvironment(void)
{
	for(map<string, BooleanDAG*>::iterator it = functionMap.begin(); it != functionMap.end(); ++it){
		it->second->clear();
		delete it->second;
	}
	if(bgproblem != NULL){
		bgproblem->clear();
		delete bgproblem;
	}
	delete finder;
	delete _pfind;
}

/* Runs the input command in interactive mode. 'cmd' can be:
 * exit -- exits the solver
 * print -- print the controls
 * import -- read the file generated by the front-end
 */
int InterpreterEnvironment::runCommand(const string& cmd, list<string*>& parlist){
	if(cmd == "exit"){
		return 0;
	}
	if(cmd == "print"){
		if(parlist.size() > 0){
			printControls(*parlist.front());
			for(list<string*>::iterator it = parlist.begin(); it != parlist.end(); ++it){
				delete *it;
			}
		}else{
			printControls("");
		}
		return -1;
	}
	if(cmd == "import"){
					
		string& fname = 	*parlist.front();
		cout<<"Reading SKETCH Program in File "<<fname<<endl;
		
		
		void* scanner;
		INp::yylex_init(&scanner);
		INp::yyset_in(fopen(fname.c_str(), "r"), scanner);			
		int tmp = INp::yyparse(scanner);
		INp::yylex_destroy(scanner);
		cout<<"DONE INPUTING"<<endl;
		for(list<string*>::iterator it = parlist.begin(); it != parlist.end(); ++it){
			delete *it;
		}
		if(tmp != 0) return tmp;
		return -1;			
	}
	
	Assert(false, "NO SUCH COMMAND"<<cmd);
	return 1;
}

/* Takes the specification (spec) and implementation (sketch) and creates a
 * single function asserting their equivalence. The Miter is created for
 * expressions 'assert sketch SKETCHES spec' in the input file to back-end.
 */
BooleanDAG* InterpreterEnvironment::prepareMiter(BooleanDAG* spec, BooleanDAG* sketch){
	if(params.verbosity > 2){
		
		cout<<"* before  EVERYTHING: "<< spec->get_name() <<"::SPEC nodes = "<<spec->size()<<"\t "<< sketch->get_name() <<"::SKETCH nodes = "<<sketch->size()<<endl;
	}

	if(params.verbosity > 2){
		cout<<" INBITS = "<<params.NINPUTS<<endl;
		cout<<" CBITS  = "<<INp::NCTRLS<<endl;
	}

	{
		Dout( cout<<"BEFORE Matching input names"<<endl );
		vector<bool_node*>& specIn = spec->getNodesByType(bool_node::SRC);
		vector<bool_node*>& sketchIn = sketch->getNodesByType(bool_node::SRC);

		int inints = 0;
		int inbits = 0;

		Assert(specIn.size() <= sketchIn.size(), "The number of inputs in the spec and sketch must match");	
		for(int i=0; i<specIn.size(); ++i){
			SRC_node* sknode = dynamic_cast<SRC_node*>(sketchIn[i]);
			SRC_node* spnode = dynamic_cast<SRC_node*>(specIn[i]);
			Dout( cout<<"Matching inputs spec: "<<sknode->name<<" with sketch: "<<spnode->name<<endl );
			sketch->rename(sknode->name, spnode->name);
			if(sketchIn[i]->getOtype() == OutType::BOOL){
				inbits++;
			}else{
				inints++;
			}
		}

		if(params.verbosity > 2){
			cout<<" input_ints = "<<inints<<" \t input_bits = "<<inbits<<endl;
		}

	}

	{
		Dout( cout<<"BEFORE Matching output names"<<endl );
		vector<bool_node*>& specDST = spec->getNodesByType(bool_node::DST);
		vector<bool_node*>& sketchDST = sketch->getNodesByType(bool_node::DST);
		Assert(specDST.size() == sketchDST.size(), "The number of inputs in the spec and sketch must match");	
		for(int i=0; i<sketchDST.size(); ++i){
			DST_node* spnode = dynamic_cast<DST_node*>(specDST[i]);
			DST_node* sknode = dynamic_cast<DST_node*>(sketchDST[i]);
			sketch->rename(sknode->name, spnode->name);			
		}
	}

	
	
	//spec->repOK();
	//sketch->repOK();

	if(params.verbosity > 1){
		cout<<" optimization level = "<<params.olevel<<endl;
	}

	if(false){
		CallGraphAnalysis cga;
		cout<<"sketch:"<<endl;
		cga.process(*sketch, functionMap);
		cout<<"spec:"<<endl;
		cga.process(*spec, functionMap);
	}
	
 	if(params.olevel >= 3){
		if(params.verbosity > 3){ cout<<" Inlining amount = "<<params.inlineAmnt<<endl; }
		{
			if(params.verbosity > 3){ cout<<" Inlining functions in the sketch."<<endl; }
			doInline(*sketch, functionMap, params.inlineAmnt);
			/*
			ComplexInliner cse(*sketch, functionMap, params.inlineAmnt, params.mergeFunctions );	
			cse.process(*sketch);
			*/
		}
		{
			if(params.verbosity > 3){ cout<<" Inlining functions in the spec."<<endl; }
			doInline(*spec, functionMap, params.inlineAmnt);
			/*
			ComplexInliner cse(*spec, functionMap,  params.inlineAmnt, params.mergeFunctions  );	
			cse.process(*spec);
			*/
		}
		
	}

	
	//spec->repOK();
	//sketch->repOK();
    Assert(spec->getNodesByType(bool_node::CTRL).size() == 0, "ERROR: Spec should not have any holes!!!");

	{
		/* Eliminates uninterpreted functions */
        //cout<<"before eliminate ufun"<<endl;
        //spec->lprint(cout);
        DagElimUFUN eufun;
		eufun.process(*spec);
        //cout<<"after eliminate ufun"<<endl;
        //spec->lprint(cout);
        
        
     	/* Assumption -- In the sketch if you have uninterpreted functions it 
can only call them with the parameters used in the spec */
		if(params.ufunSymmetry){ eufun.stopProducingFuns(); }
        //cout<<"before eliminate ufun"<<endl;
        sketch->lprint(cout);
		
		eufun.process(*sketch);
       // cout<<"after eliminate ufun"<<endl;
        //sketch->lprint(cout);
        
	}
    
    {
        //Post processing to replace ufun inputs with tuple of src nodes.
        replaceSrcWithTuple(*spec);
        replaceSrcWithTuple(*sketch);
    }
	//At this point spec and sketch may be inconsistent, because some nodes in spec will have nodes in sketch as their children.
    cout<<"before make miter"<<endl;
    sketch->lprint(cout);
	spec->makeMiter(sketch);
	BooleanDAG* result = spec;
    cout<<"after make miter"<<endl;
    result->lprint(cout);
	
	
	if(params.verbosity > 2){ cout<<"after Creating Miter: Problem nodes = "<<result->size()<<endl; }
		

	return runOptims(result);
}

void InterpreterEnvironment::replaceSrcWithTuple(BooleanDAG& dag) {
    vector<bool_node*> newnodes;
    for(int i=0; i<dag.size(); ++i ){
		if (dag[i]->type == bool_node::SRC) {
            SRC_node* srcNode = dynamic_cast<SRC_node*>(dag[i]);
            if (srcNode->isTuple) {
                Tuple* outputsType = dynamic_cast<Tuple*>(OutType::getTuple(srcNode->tupleName));
                string name = srcNode->get_name();
                TUPLE_CREATE_node* outputs = new TUPLE_CREATE_node();
                outputs->setName(srcNode->tupleName);
                int size = outputsType->entries.size();
                for (int j = 0; j < size ; j++) {
                    stringstream str;
                    str<<name<<"_"<<j;
                    SRC_node* src =  new SRC_node( str.str() );
                    OutType* type = outputsType->entries[j];
                    int nbits = 0;
                    if (type == OutType::BOOL || type == OutType::BOOL_ARR) {
                        nbits = 1;
                    }
                    if (type == OutType::INT || type == OutType::INT_ARR) {
                        nbits = 2;
                    }
                    if (nbits > 1) { nbits = PARAMS->NANGELICS; }
                    src->set_nbits(nbits);
                    //if(node.getOtype() == bool_node::INT_ARR || node.getOtype() == bool_node::BOOL_ARR){
                    if(type == OutType::INT_ARR || type == OutType::BOOL_ARR) {
                        // TODO xzl: is this fix correct?
                        // will this be used with angelic CTRL? see Issue #5 and DagFunctionInliner
                        //int sz = PARAMS->angelic_arrsz;
                        
                        //This should be changed
                        int sz = 1 << PARAMS->NINPUTS;
                        //int sz = 1;
                        //for(int i=0; i<PARAMS->NINPUTS; ++i){
                        //	sz = sz *2;
                        //}
                        src->setArr(sz);
                    }
                    newnodes.push_back(src);
                    outputs->multi_mother.push_back(src);
                }
                outputs->addToParents();
                newnodes.push_back(outputs);
                dag.replace(i, outputs);
                
            }
        }
	}
    
    dag.addNewNodes(newnodes);
	newnodes.clear();
    //cout<<"after replacing src with tuples"<<endl;
    //dag.lprint(cout);

	dag.removeNullNodes();
}


void InterpreterEnvironment::doInline(BooleanDAG& dag, map<string, BooleanDAG*> functionMap, int steps){	
	//OneCallPerCSiteInliner fin;
	InlineControl* fin = new OneCallPerCSiteInliner(); //new BoundedCountInliner(PARAMS->boundedCount);
	/*
	if(PARAMS->boundedCount > 0){
		fin = new BoundedCountInliner(PARAMS->boundedCount);
	}else{
		fin = new OneCallPerCSiteInliner();
	}	 
	*/
	DagFunctionInliner dfi(dag, functionMap, fin);	
	int oldSize = -1;
	bool nofuns = false;
	for(int i=0; i<steps; ++i){
		int t = 0;
		do{
            dfi.process(dag);
            // dag.repOK();
			set<string>& dones = dfi.getFunsInlined();			
			// dag.lprint(cout);
			if(params.verbosity> 3){ cout<<"inlined "<<dfi.nfuns()<<" new size ="<<dag.size()<<endl; }
			if(oldSize > 0){
				if(dag.size() > 400000 && dag.size() > oldSize * 10){
					i=steps;
					cout<<"WARNING: Preemptively stopping inlining because the graph was growing too big too fast"<<endl; 
					break;
				}
			}
			oldSize = dag.size();
			++t;			
		}while(dfi.changed());
		cout<<"END OF STEP "<<i<<endl;
		// fin.ctt.printCtree(cout, dag);
		fin->clear();
		if(t==1){ cout<<"Bailing out"<<endl; break; }
	}
	{
		DagFunctionToAssertion makeAssert(dag, functionMap);
		makeAssert.process(dag);
	}
	delete fin;
}



int InterpreterEnvironment::assertDAG(BooleanDAG* dag, ostream& out){
	Assert(status==READY, "You can't do this if you are UNSAT");
	++assertionStep;	
	
	solver->addProblem(dag);
	
//	cout << "InterpreterEnvironment: new problem" << endl;
//	problem->lprint(cout);

	if(params.superChecks){
		history.push_back(dag->clone());	
	}

	// problem->repOK();
	
		
  	
	if(params.outputEuclid){      		
		ofstream fout("bench.ucl");
		solver->outputEuclid(fout);
	}
  	
	if(params.output2QBF){
		string fname = basename();
		fname += "_2qbf.cnf";
		ofstream out(fname.c_str());
		cout<<" OUTPUTING 2QBF problem to file "<<fname<<endl;
		solver->setup2QBF(out);		
	}
  	
  		
	int solveCode = 0;
	try{
		
		solveCode = solver->solve();
		
		solver->get_control_map(currentControls);
	}catch(SolverException* ex){
		cout<<"ERROR "<<basename()<<": "<<ex->code<<"  "<<ex->msg<<endl;
		status=UNSAT;				
		return ex->code + 2;
	}catch(BasicError& be){
		solver->get_control_map(currentControls);
		cout<<"ERROR: "<<basename()<<endl;
		status=UNSAT;				
		return 3;
	}
	if( !solveCode ){
		status=UNSAT;				
		return 1;	
	}

	if(false){
		statehistory.push_back(solver->find_history);

		for(int i=0; i<history.size(); ++i){
			cout<<" ~~~ Order = "<<i<<endl;
			BooleanDAG* bd = solver->hardCodeINode(history[i], solver->ctrlStore, bool_node::CTRL);
			int sz = bd->getNodesByType(bool_node::ASSERT).size();
			cout<<" ++ Order = "<<i<<" size = "<<sz<<endl;
			if(sz > 0){
				Strudel st(statehistory[i], &finder->getMng());
				st.checker(history[i] , solver->ctrlStore, bool_node::CTRL);
			}
		}
	}
		
	return 0;

}

int InterpreterEnvironment::assertDAG_wrapper(BooleanDAG* dag){
	ostream& out = std::cout;
	return assertDAG(dag, out);
}

int InterpreterEnvironment::assertDAG_wrapper(BooleanDAG* dag, const char* fileName){
	ofstream out(fileName, ios_base::out);
	return assertDAG(dag, out);
}

BooleanDAG* InterpreterEnvironment::runOptims(BooleanDAG* result){	
	
	if(params.olevel >= 3){
		DagOptim cse(*result);	
		//cse.alterARRACS();
		cse.process(*result);
	}
	// result->repOK();

	if(params.verbosity > 3){cout<<"* after OPTIM: Problem nodes = "<<result->size()<<endl;	}
	/*{
		DagOptim op(*result);
		result->replace(5598, op.getCnode(1));
		op.process(*result);
	}*/

	
	
	if(false && params.olevel >= 5){		
		BackwardsAnalysis opt;
		cout<<"BEFORE ba: "<<endl;
		//result->print(cout);
		opt.process(*result);
		cout<<"AFTER ba: "<<endl;
		// result->print(cout);
	}
	// result->repOK();
	if(params.olevel >= 7){
		cout << "BEFORE OptimizeCommutAssoc"<< result->size() << endl;
		DagOptimizeCommutAssoc opt;
		opt.process(*result);
		cout << "AFTER OptimizeCommutAssoc "<<result->size() << endl;
	}
	// result->repOK();
	//result->print(cout) ;

	// cout<<"* after CAoptim: Problem nodes = "<<result->size()<<endl;

	if(params.olevel >= 4){
		cout << "BEFORE cse" << endl;
		DagOptim cse(*result);	
		if(params.alterARRACS){ 
			cout<<" alterARRACS"<<endl;
			cse.alterARRACS(); 
		}
		cse.process(*result);
		cout << "AFTER cse" << endl;
	}
	// result->repOK();	
	if(params.verbosity > 0){ cout<<"* Final Problem size: Problem nodes = "<<result->size()<<endl;	}
	if(params.showDAG){ 
		result->lprint(cout);		
	}
	if(params.outputMRDAG){
		ofstream of(params.mrdagfile.c_str());
		cout<<"Outputing Machine Readable DAG to file "<<params.mrdagfile<<endl;
		result->mrprint(of);
		of.close();
	}
    cout<<"After optims"<<endl;
    result->lprint(cout);
    
	return result;
}
