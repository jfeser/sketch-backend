#include "InterpreterEnvironment.h"
//#include "ABCSATSolver.h"
#include "InputReader.h"
#include "CallGraphAnalysis.h"
#include "ComplexInliner.h"
#include "DagFunctionToAssertion.h"
#include "InputReader.h" // INp yylex_init, yyparse, etc.
#include "ArithmeticExpressionBuilder.h"
#include "SwapperPredicateBuilder.h"
#include "DeductiveSolver.h"


#ifdef CONST
#undef CONST
#endif



class Strudel {
	vector<Tvalue>& vals;
	SATSolver* solver;
	FloatManager& floats;
public:
	Strudel(vector<Tvalue>& vtv, SATSolver* solv, FloatManager& fm):vals(vtv), solver(solv), floats(fm){
		cout << "This is strange size=" << vtv.size() << endl;
	}




	int valueForINode(INTER_node* inode, VarStore& values, int& nbits) {
		Tvalue& tv = vals[inode->id];
		int retval = tv.eval(solver);
		{ cout << " input " << inode->get_name() << " has value " << retval << endl; }
		return retval;
	}

	void checker(BooleanDAG* dag, VarStore& values, bool_node::Type type) {
		cout << "Entering ~!" << endl;
		BooleanDAG* newdag = dag->clone();
		vector<bool_node*> inodeList = newdag->getNodesByType(type);

		// cout<<" * Specializing problem for "<<(type == bool_node::CTRL? "controls" : "inputs")<<endl; 
		cout << " * Before specialization: nodes = " << newdag->size() << " Ctrls = " << inodeList.size() << endl;
		{
			DagOptim cse(*newdag, floats);			

			BooleanDAG* cl = newdag->clone();
			for (int i = 0; i<newdag->size(); ++i) {
				// Get the code for this node.				
				if ((*newdag)[i] != NULL) {
					if ((*newdag)[i]->type == bool_node::CTRL) {
						INTER_node* inode = dynamic_cast<INTER_node*>((*newdag)[i]);
						int nbits;
						int t = valueForINode(inode, values, nbits);
						bool_node * repl = NULL;
						if (nbits == 1) {
							repl = cse.getCnode(t == 1);
						}
						else {
							repl = cse.getCnode(t);
						}
						Tvalue& tv = vals[i];
						cout << "ctrl=";
						tv.print(cout, solver);
						cout << endl;
						Assert((*newdag)[inode->id] == inode, "The numbering is wrong!!");
						newdag->replace(inode->id, repl);
					}
					else {
						bool_node* node = cse.computeOptim((*newdag)[i]);
						Tvalue& tv = vals[i];
						cout << " old = " << (*cl)[i]->lprint() << " new " << node->lprint() << "  ";
						tv.print(cout, solver);
						if (node->type == bool_node::CONST && cse.getIval(node) == tv.eval(solver)) {
							cout << " good";
						}
						else {
							cout << " bad";
						}
						cout << endl;

						if ((*newdag)[i] != node) {
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
	for (map<string, BooleanDAG*>::iterator it = functionMap.begin(); it != functionMap.end(); ++it) {
		it->second->clear();
		delete it->second;
	}
	if (bgproblem != NULL) {
		bgproblem->clear();
		delete bgproblem;
	}
	ArithExprBuilder::clearStaticMapMemory();
	SwapperPredicateNS::PredicateBuilder::clearStaticMapMemory();
	delete finder;
	delete _pfind;
}

/* Runs the input command in interactive mode. 'cmd' can be:
* exit -- exits the solver
* print -- print the controls
* import -- read the file generated by the front-end
*/
int InterpreterEnvironment::runCommand(const string& cmd, list<string*>& parlist) {
	if (cmd == "exit") {
		return 0;
	}
	if (cmd == "print") {
		if (parlist.size() > 0) {
			printControls(*parlist.front());
			for (list<string*>::iterator it = parlist.begin(); it != parlist.end(); ++it) {
				delete *it;
			}
		}
		else {
			printControls("");
		}
		return -1;
	}
	if (cmd == "import") {

		string& fname = *parlist.front();
		cout << "Reading SKETCH Program in File " << fname << endl;


		void* scanner;
		INp::yylex_init(&scanner);
		INp::yyset_in(fopen(fname.c_str(), "r"), scanner);
		int tmp = INp::yyparse(scanner);
		INp::yylex_destroy(scanner);
		cout << "DONE INPUTING" << endl;
		for (list<string*>::iterator it = parlist.begin(); it != parlist.end(); ++it) {
			delete *it;
		}
		if (tmp != 0) return tmp;
		return -1;
	}

	Assert(false, "NO SUCH COMMAND" << cmd);
	return 1;
}

/* Takes the specification (spec) and implementation (sketch) and creates a
* single function asserting their equivalence. The Miter is created for
* expressions 'assert sketch SKETCHES spec' in the input file to back-end.
*/
BooleanDAG* InterpreterEnvironment::prepareMiter(BooleanDAG* spec, BooleanDAG* sketch, int inlineAmnt){
	if (params.verbosity > 2) {

		cout << "* before  EVERYTHING: " << spec->get_name() << "::SPEC nodes = " << spec->size() << "\t " << sketch->get_name() << "::SKETCH nodes = " << sketch->size() << endl;
	}

	if (params.verbosity > 2) {
		cout << " INBITS = " << params.NINPUTS << endl;
		cout << " CBITS  = " << INp::NCTRLS << endl;
	}

	{
		Dout(cout << "BEFORE Matching input names" << endl);
		vector<bool_node*>& specIn = spec->getNodesByType(bool_node::SRC);
		vector<bool_node*>& sketchIn = sketch->getNodesByType(bool_node::SRC);

		int inints = 0;
		int inbits = 0;

		Assert(specIn.size() <= sketchIn.size(), "The number of inputs in the spec and sketch must match");
		for (int i = 0; i<specIn.size(); ++i) {
			SRC_node* sknode = dynamic_cast<SRC_node*>(sketchIn[i]);
			SRC_node* spnode = dynamic_cast<SRC_node*>(specIn[i]);
			Dout(cout << "Matching inputs spec: " << sknode->name << " with sketch: " << spnode->name << endl);
			sketch->rename(sknode->name, spnode->name);
			if (sketchIn[i]->getOtype() == OutType::BOOL) {
				inbits++;
			}
			else {
				inints++;
			}
      if (sknode->isTuple) {
        if (sknode->depth == -1)
          sknode->depth = params.srcTupleDepth;
      }
		}

		if (params.verbosity > 2) {
			cout << " input_ints = " << inints << " \t input_bits = " << inbits << endl;
		}

	}

	{
		Dout(cout << "BEFORE Matching output names" << endl);
		vector<bool_node*>& specDST = spec->getNodesByType(bool_node::DST);
		vector<bool_node*>& sketchDST = sketch->getNodesByType(bool_node::DST);
		Assert(specDST.size() == sketchDST.size(), "The number of inputs in the spec and sketch must match");
		for (int i = 0; i<sketchDST.size(); ++i) {
			DST_node* spnode = dynamic_cast<DST_node*>(specDST[i]);
			DST_node* sknode = dynamic_cast<DST_node*>(sketchDST[i]);
			sketch->rename(sknode->name, spnode->name);
		}
	}



	//spec->repOK();
	//sketch->repOK();


	if (false) {
		CallGraphAnalysis cga;
		cout << "sketch:" << endl;
		cga.process(*sketch, functionMap, floats);
		cout << "spec:" << endl;
		cga.process(*spec, functionMap, floats);
	}

	if (params.olevel >= 3) {
		if(params.verbosity > 3){ cout<<" Inlining amount = "<<inlineAmnt<<endl; }
		{
			if (params.verbosity > 3) { cout << " Inlining functions in the sketch." << endl; }
			try {
				doInline(*sketch, functionMap, inlineAmnt, replaceMap);
			}catch (BadConcretization& bc) {
				sketch->clear();
				spec->clear();
				delete sketch;
				delete spec;
				throw bc;
			}
			

			/*
			ComplexInliner cse(*sketch, functionMap, params.inlineAmnt, params.mergeFunctions );
			cse.process(*sketch);
			*/
		}
		{
			if (params.verbosity > 3) { cout << " Inlining functions in the spec." << endl; }
			try {
				doInline(*spec, functionMap, inlineAmnt, replaceMap);
			} catch (BadConcretization& bc) {
				sketch->clear();
				spec->clear();
				delete sketch;
				delete spec;
				throw bc;
			}
			

			/*
			ComplexInliner cse(*spec, functionMap,  params.inlineAmnt, params.mergeFunctions  );
			cse.process(*spec);
			*/
		}

	}
	//spec->repOK();
	//sketch->repOK();
	Assert(spec->getNodesByType(bool_node::CTRL).size() == 0, "ERROR: Spec should not have any holes!!!");

  if (params.numericalSolver) {
    // Abstract the numerical part from the dag
    // TODO: what is the best place to have this
    abstractNumericalPart(*sketch);
  }
  
  if(false){
		/* Eliminates uninterpreted functions */
		DagElimUFUN eufun;
		eufun.process(*spec);


		/* ufunSymmetry optimizes based on the following Assumption: -- In the sketch if you have uninterpreted functions it
		can only call them with the parameters used in the spec */
		if (params.ufunSymmetry) { eufun.stopProducingFuns(); }
		eufun.process(*sketch);

	}

	{
		//Post processing to replace ufun inputs with tuple of src nodes.
       replaceSrcWithTuple(*spec);
       replaceSrcWithTuple(*sketch);
	}
	//At this point spec and sketch may be inconsistent, because some nodes in spec will have nodes in sketch as their children.
	spec->makeMiter(sketch);
	BooleanDAG* result = spec;
	
	if (params.verbosity > 6) { cout << "after Creating Miter: Problem nodes = " << result->size() << endl; }


	result = runOptims(result);
  return result;
}

bool_node* createTupleSrcNode(string tuple_name, string node_name, int depth, vector<bool_node*>& newnodes, bool ufun) {
	if (depth == 0) {
		CONST_node* cnode = CONST_node::create(-1);
		newnodes.push_back(cnode);
		return cnode;
	}

	Tuple* tuple_type = dynamic_cast<Tuple*>(OutType::getTuple(tuple_name));
	int size = tuple_type->actSize;
	TUPLE_CREATE_node* new_node = TUPLE_CREATE_node::create(tuple_type->entries.size());
	new_node->depth = depth;
	new_node->setName(tuple_name);
	
	for (int j = 0; j < size; j++) {
		stringstream str;
		str << node_name << "_" << j;

		OutType* type = tuple_type->entries[j];

		if (type->isTuple) {
			new_node->set_parent(j, createTupleSrcNode(((Tuple*)type)->name, str.str(), depth - 1, newnodes, ufun));
		}
		else if (type->isArr && ((Arr*)type)->atype->isTuple) {
			CONST_node* cnode = CONST_node::create(-1);
			newnodes.push_back(cnode);
			new_node->set_parent(j, cnode);

		}
		else {

			SRC_node* src = SRC_node::create(str.str());

			int nbits = 0;
			if (type == OutType::BOOL || type == OutType::BOOL_ARR) {
				nbits = 1;
			}
			if (type == OutType::INT || type == OutType::INT_ARR) {
				nbits = 2;
			}

			if (nbits > 1) { nbits = PARAMS->NANGELICS; }
			src->set_nbits(nbits);
			if (type == OutType::INT_ARR || type == OutType::BOOL_ARR) {
				int sz = 1 << PARAMS->NINPUTS;
				src->setArr(sz);
			}
			newnodes.push_back(src);

			new_node->set_parent(j, src);
		}
	}

	CONST_node* cnode = CONST_node::create(-1);
	newnodes.push_back(cnode);
	for (int i = size; i < tuple_type->entries.size(); i++) {
		new_node->set_parent(i, cnode);
	}
	new_node->addToParents();
	newnodes.push_back(new_node);

	if (ufun) return new_node;

	
	stringstream str;
	str << node_name << "__";

	SRC_node* src = SRC_node::create(str.str());
	src->set_nbits(1);
	newnodes.push_back(src);

	ARRACC_node* ac = ARRACC_node::create(src, cnode, new_node);
	
	ac->addToParents();
	newnodes.push_back(ac);
	return ac;
}


void InterpreterEnvironment::replaceSrcWithTuple(BooleanDAG& dag) {
	vector<bool_node*> newnodes;
	for (int i = 0; i<dag.size(); ++i) {
		if (dag[i]->type == bool_node::SRC) {
			SRC_node* srcNode = dynamic_cast<SRC_node*>(dag[i]);
			if (srcNode->isTuple) {
				int depth = srcNode->depth;
				if (depth == -1) depth = params.srcTupleDepth;
				bool_node* new_node = createTupleSrcNode(srcNode->tupleName, srcNode->get_name(), depth, newnodes, srcNode->ufun);
				dag.replace(i, new_node);
			}
		}
	}

	dag.addNewNodes(newnodes);
	newnodes.clear();
	dag.removeNullNodes();
}



void findPureFuns(map<string, BooleanDAG*>& functionMap, set<string>& pureFuns) {

	for (auto it = functionMap.begin(); it != functionMap.end(); ++it) {
		vector<bool_node*>& ctrlvec = it->second->getNodesByType(bool_node::CTRL);
		if (ctrlvec.size() == 0) {
			pureFuns.insert(it->first);
			continue;
		}
		if (ctrlvec.size() == 1 && ctrlvec[0]->get_name() == "#PC") {
			pureFuns.insert(it->first);
		}
	}

	set<string> other;
	do{
		other = pureFuns;
		for (auto it = pureFuns.begin(); it != pureFuns.end(); ++it) {
			BooleanDAG* bd = functionMap[*it];

			vector<bool_node*>& ufvec = bd->getNodesByType(bool_node::UFUN);
			for (auto ufit = ufvec.begin(); ufit != ufvec.end(); ++ufit ) {
				
				UFUN_node* ufn = dynamic_cast<UFUN_node*>(*ufit);
				if (ufn == NULL) { continue;  }
				if (other.count(ufn->get_ufname()) == 0) {
					//calling a non-pure function means you are not pure either.
					other.erase(*it);
					break;
				}
			}
		}
		swap(other, pureFuns);
	} while (other.size() != pureFuns.size());

}

void InterpreterEnvironment::doInline(BooleanDAG& dag, map<string, BooleanDAG*>& functionMap, int steps, map<string, map<string, string> > replaceMap){
	//OneCallPerCSiteInliner fin;
	// InlineControl* fin = new OneCallPerCSiteInliner(); //new BoundedCountInliner(PARAMS->boundedCount);
	TheBestInliner fin(steps, params.boundmode == CommandLineArgs::CALLSITE);
	/*
	if(PARAMS->boundedCount > 0){
	fin = new BoundedCountInliner(PARAMS->boundedCount);
	}else{
	fin = new OneCallPerCSiteInliner();
	}
	*/


	set<string> pureFuns;

	findPureFuns(functionMap, pureFuns);

	DagFunctionInliner dfi(dag, functionMap, replaceMap, floats, &hardcoder, pureFuns, params.randomassign, &fin, params.onlySpRandAssign,
                         params.spRandBias); 




	int oldSize = -1;
	bool nofuns = false;
	for (int i = 0; i<steps; ++i) {
		int t = 0;
    int ct = 0;
		do {
			if (params.randomassign && params.onlySpRandAssign) {
				if (ct < 2) {
				dfi.turnOffRandomization();
				ct++;
				} else {
				dfi.turnOnRandomization();
				}
			}			
			dfi.process(dag);
			//
			// dag.repOK();
			set<string>& dones = dfi.getFunsInlined();
			if (params.verbosity> 6) { cout << "inlined " << dfi.nfuns() << " new size =" << dag.size() << endl; }
			//dag.lprint(cout);
			if (params.bndDAG > 0 && dag.size() > params.bndDAG) {
				cout << "WARNING: Preemptively stopping CEGIS because the graph size exceeds the limit: " << params.bndDAG << endl;
				exit(1);
			}
			if (oldSize > 0) {
				if(dag.size() > 400000000 && dag.size() > oldSize * 10){
					i = steps;
					cout << "WARNING: Preemptively stopping inlining because the graph was growing too big too fast" << endl;
					break;
				}
				if((dag.size() > 400000 && dag.size() > oldSize * 2)|| dag.size() > 1000000){
					hardcoder.tryHarder();
				}
			}
			oldSize = dag.size();
			++t;
		} while (dfi.changed());
		if (params.verbosity> 6) { cout << "END OF STEP " << i << endl; }
		// fin.ctt.printCtree(cout, dag);

		fin.clear();
		if (t == 1 && params.verbosity> 6) { cout << "Bailing out" << endl; break; }
	}
	hardcoder.afterInline();
	{
		DagFunctionToAssertion makeAssert(dag, functionMap, floats);
		makeAssert.process(dag);
	}
	
}


ClauseExchange::ClauseExchange(MiniSATSolver* ms, const string& inf, const string& outf)
	:msat(ms), infile(inf), outfile(outf)
{
	failures = 0;

	msat->getShareable(single, baseline, dble);

	//Wipe the files clean
	{
	FILE* f = fopen(infile.c_str(), "w");	
	fclose(f);
	}
	{
	FILE* f = fopen(outfile.c_str(), "w");	
	fclose(f);
	}	
}

void ClauseExchange::exchange(){
	analyzeLocal();
	int ssize = single.size();
	int dsize = dble.size();	
	if (PARAMS->verbosity > 8) {
		cout << "Before readInfile" << endl;
		printToExchange();
	}
	

	readInfile();

	if (PARAMS->verbosity > 8) {
		cout << "After readInfile" << endl;
		printToExchange();
	}

	if(ssize > 0 || dsize > 0){
		pushOutfile();
	}
}

void ClauseExchange::readInfile(){
	int ssize = single.size();
	int dsize = dble.size();
	int bufsize = ssize + dsize*2 + 3;
	bufsize = bufsize * 3;
	bufsize = max(bufsize, 200);
	vector<int> sbuf(bufsize);
	FILE* f = fopen(outfile.c_str(), "r");
	int rsize = fread(&sbuf[0], sizeof(int), bufsize, f);
	if(rsize < 3){
		fclose(f);
		cout<<"Nothing read"<<endl;
		return;
	}
	ssize = sbuf[0]; dsize=sbuf[1];
	int realsize = ssize + dsize*2 + 3;
	if(rsize != realsize){
		if(rsize > realsize){
			fclose(f);
			cout<<"Corrupted"<<endl;
			return;
		}
		if(rsize != bufsize){
			fclose(f);
			cout<<"Corrupted"<<endl;
			return;
		}
		sbuf.resize(realsize);
		rsize = fread(&sbuf[bufsize], sizeof(int), realsize - bufsize, f);
		fclose(f);
		if(rsize + bufsize != realsize){
			cout<<"Corrupted"<<endl;
			return;
		}
	}else{
		fclose(f);
		sbuf.resize(realsize);
	}
	cout << "Received: ";
	for (int i = 0; i < sbuf.size(); ++i) {
		cout << ", " << sbuf[i];
	}
	cout << endl;
	unsigned chksum = 0;
	for(int i=0; i<sbuf.size()-1; ++i){
		chksum += sbuf[i];
	}
	if(chksum != sbuf[sbuf.size()-1]){
		cout<<"Failed checksum"<<endl;
		return;
	}
	{
		vec<Lit> vl(1);
		for(int i=2; i < 2+ssize; ++i){
			int sin = sbuf[i];
			if(single.count(sin)==0){
				single.insert(sin);
				vl[0] = toLit(sin);
				msat->addHelperClause(vl);
			}
		}
	}
	{
		vec<Lit> vl(2);
		for(int i=2+ssize; i< sbuf.size()-1; i+=2){
			int f = sbuf[i];
			int s = sbuf[i+1];
			pair<int, int> p = make_pair(f, s);
			if(dble.count(p) ==0){
				dble.insert(p);
				vl[0] = toLit(f); vl[1] = toLit(s);
				msat->addHelperClause(vl);
			}
		}
	}
}

void ClauseExchange::pushOutfile(){
	int ssize = single.size();
	int dsize = dble.size();
	unsigned chksum = 0;
	vector<int> sbuf(ssize + dsize*2 + 3);
	chksum += ssize;
	chksum += dsize;
	sbuf[0] = ssize;
	sbuf[1] = dsize;
	int i=2;
	for(set<int>::iterator it = single.begin(); it != single.end(); ++it){
		chksum += *it;
		sbuf[i] = *it; ++i;
	}
	for(set<pair<int, int> >::iterator it = dble.begin(); it != dble.end(); ++it){
		chksum += it->first;
		chksum += it->second;
		sbuf[i] = it->first; ++i;
		sbuf[i] = it->second; ++i;
	}
	sbuf[i] = chksum;
	cout << "Sending: " << endl;
	for (int ii = 0; ii < sbuf.size(); ++ii) {
		cout << ", " << sbuf[ii];
	}
	cout << endl;
	FILE* f = fopen(outfile.c_str(), "w");
	fwrite(&sbuf[0], sizeof(int), i+1, f);
	fclose(f);
}

void ClauseExchange::analyzeLocal(){
	single.clear(); dble.clear();
	msat->getShareable(single, dble, baseline);	
}


void InterpreterEnvironment::share(){
	if(exchanger!=NULL){
		exchanger->exchange();
	}
}



void InterpreterEnvironment::fixes(const string& holename) {
	int pos = spskpairs.size();
	if (holesToHardcode.size() <= pos) {
		holesToHardcode.resize(pos + 1);
	}
	Assert(pos > 0, "CAN'T HAPPEN!");
	holesToHardcode[pos-1].push_back(holename);
}




int InterpreterEnvironment::doallpairs() {
	int howmany = params.ntimes;
	if (howmany < 1 || !params.randomassign) { howmany = 1; }
	SATSolver::SATSolverResult result = SATSolver::UNDETERMINED;


	// A dummy ctrl for inlining bound
	CTRL_node* inline_ctrl = NULL;
	if (params.randomInlining) {
		inline_ctrl = CTRL_node::create();
		inline_ctrl->name = "inline";
		hardcoder.declareControl(inline_ctrl);
	}

	if (acEnabled()) {
		hardcoder.registerAllControls(functionMap);
		if (exchanger == NULL && howmany > 5) {
			string inf = params.inputFname;
			inf += ".com";
			exchanger = new ClauseExchange(hardcoder.getMiniSat(), inf, inf);
		}
	}
	string errMsg;
	maxRndSize = 0;
	hardcoder.setHarnesses(spskpairs.size());
	for (int trailID = 0; trailID<howmany; ++trailID) {
		if (howmany>1) { cout << "ATTEMPT " << trailID << endl; }
		if (trailID % 5 == 4) {
			share();
			if (params.randdegree == 0) {
				hardcoder.adjust();
			}
		}
		if (params.randdegree == 0) {
			hardcoder.setRanddegree(trailID);
		}

		timerclass roundtimer("Round");
		roundtimer.start();

		// Fix a random value to the inlining bound
		int inlineAmnt = params.inlineAmnt;
		int minInlining = 3;
		if (params.inlineAmnt > minInlining && params.randomInlining) {
			inline_ctrl->special_concretize(params.inlineAmnt - minInlining);
			hardcoder.fixValue(*inline_ctrl, params.inlineAmnt - minInlining, 5);
			inlineAmnt = hardcoder.getValue(inline_ctrl->name) + minInlining;
		}
		for (int i = 0; i<spskpairs.size(); ++i) {
			hardcoder.setCurHarness(i);
			try {
				BooleanDAG* bd = prepareMiter(getCopy(spskpairs[i].spec), getCopy(spskpairs[i].sketch), inlineAmnt);
				result = assertDAG(bd, cout, spskpairs[i].file);
				cout << "RESULT = " << result << "  " << endl;;
				printControls("");
			}
			catch (BadConcretization& bc) {
				errMsg = bc.msg;
				hardcoder.dismissedPending();
				result = SATSolver::UNSATISFIABLE;
				break;
			}
			if (result != SATSolver::SATISFIABLE) {
				break;
			}
			if (hardcoder.isDone()) {
				break;
			}
			if (i < holesToHardcode.size()) {
				auto tohardcode = holesToHardcode[i];
				for (auto holes = tohardcode.begin(); holes != tohardcode.end(); ++holes) {
					Tvalue& tv = finder->getControl(*holes);
					auto val = solver->ctrlStore[*holes];
					hardcoder.settleHole(*holes, val);
					if (tv.isSparse()) {
						for (int idx = 0; idx < tv.getSize(); ++idx) {
							auto gv = tv.num_ranges[idx];
							if (gv.value == val) {
								finder->addAssertClause(gv.guard);
							}
							else {
								finder->addAssertClause(-gv.guard);
							}
						}
					}
					else {
						if (val == 1) {
							finder->addAssertClause(tv.getId());
						}
						else {
							finder->addAssertClause(-tv.getId());
						}
					}

				}
			}

		}
		roundtimer.stop();
		cout << "**ROUND " << trailID << " : " << hardcoder.getTotsize() << " ";
		roundtimer.print("time");
		cout << "RNDDEG = " << hardcoder.getRanddegree() << endl;
		double comp = log(roundtimer.get_cur_ms()) + hardcoder.getTotsize();
		hardcoder.addScore(comp);
		if (result == SATSolver::SATISFIABLE) {
			cout << "return 0" << endl;
			if (params.minvarHole) {
				resetMinimize();
				if (hardcoder.isDone()) {
					return 0;
				}
			}
			else {
				return 0;
			}
		}
		else {
			if (finder->lastErrMsg != "") {
				errMsg = finder->lastErrMsg;
			}
			reset();
			if (hardcoder.isDone()) {
				if (hasGoodEnoughSolution) {
					cout << "return 0" << endl;
					return 0;
				}
				else {
					cerr << errMsg << endl;
					cout << "return 1" << endl;
					return 1;
				}
			}
		}
	}
	cout << "return 2" << endl;
	return 2; // undefined.
}







SATSolver::SATSolverResult InterpreterEnvironment::assertDAG(BooleanDAG* dag, ostream& out, const string& file) {
	Assert(status == READY, "You can't do this if you are UNSAT");
	++assertionStep;

	
	solver->addProblem(dag, file);
	

	//	cout << "InterpreterEnvironment: new problem" << endl;
	//	problem->lprint(cout);

	if (params.superChecks) {
		history.push_back(dag->clone());
	}

	// problem->repOK();



	if (params.outputEuclid) {
		ofstream fout("bench.ucl");
		solver->outputEuclid(fout);
	}

	if (params.output2QBF) {
		string fname = basename();
		fname += "_2qbf.cnf";
		ofstream out(fname.c_str());
		cout << " OUTPUTING 2QBF problem to file " << fname << endl;
		solver->setup2QBF(out);
	}

	if (dag->useSymbolic()) {		
		DeductiveSolver deductive(dag, this->floats);
		deductive.symbolicSolve(*this->finder);	
		

		solver->ctrlStore.synths.clear();
		auto end = this->finder->get_sins().end();
		for (auto it = this->finder->get_sins().begin(); it != end; ++it) {
			solver->ctrlStore.synths[it->first] = it->second;
		}
		solver->ctrlStore.finalizeSynthOutputs();
		recordSolution();
		return SATSolver::SATISFIABLE;
	}


	int solveCode = 0;
	try {

		solveCode = solver->solve();
		if (solveCode || !hasGoodEnoughSolution) {
			recordSolution();
		}
	}
	catch (SolverException* ex) {
		cout << "ERROR " << basename() << ": " << ex->code << "  " << ex->msg << endl;
		status = UNSAT;
		return ex->code;
	}
	catch (BasicError& be) {
		if (!hasGoodEnoughSolution) {
			recordSolution();
		}
		cout << "ERROR: " << basename() << endl;
		status = UNSAT;
		return SATSolver::ABORTED;
	}
	if (!solveCode) {
		status = UNSAT;
		return SATSolver::UNSATISFIABLE;
	}

	if (false) {
		statehistory.push_back(solver->find_history);

		for (int i = 0; i<history.size(); ++i) {
			cout << " ~~~ Order = " << i << endl;
			BooleanDAG* bd = solver->hardCodeINode(history[i], solver->ctrlStore, bool_node::CTRL);
			int sz = bd->getNodesByType(bool_node::ASSERT).size();
			cout << " ++ Order = " << i << " size = " << sz << endl;
			if (sz > 0) {
				Strudel st(statehistory[i], &finder->getMng(), floats);
				st.checker(history[i], solver->ctrlStore, bool_node::CTRL);
			}
		}
	}

	return SATSolver::SATISFIABLE;
}

int InterpreterEnvironment::assertDAG_wrapper(BooleanDAG* dag) {
	ostream& out = std::cout;
	return assertDAG(dag, out, "");
}

int InterpreterEnvironment::assertDAG_wrapper(BooleanDAG* dag, const char* fileName) {
	ofstream out(fileName, ios_base::out);
	return assertDAG(dag, out, "");
}

BooleanDAG* InterpreterEnvironment::runOptims(BooleanDAG* result){		
	if (params.olevel >= 3) {
		DagOptim cse(*result, floats);	
		//cse.alterARRACS();
		cse.process(*result);
	}
	// result->repOK();

	// if(params.verbosity > 3){cout<<"* after OPTIM: Problem nodes = "<<result->size()<<endl;	}
	/*{
	DagOptim op(*result);
	result->replace(5598, op.getCnode(1));
	op.process(*result);
	}*/



	if (false && params.olevel >= 5) {
		BackwardsAnalysis opt;
		cout << "BEFORE ba: " << endl;
		//result->print(cout);
		opt.process(*result);
		cout << "AFTER ba: " << endl;
		// result->print(cout);
	}
	// result->repOK();
	if (params.olevel >= 7) {
		cout << "BEFORE OptimizeCommutAssoc" << result->size() << endl;
		DagOptimizeCommutAssoc opt;
		opt.process(*result);
		cout << "AFTER OptimizeCommutAssoc " << result->size() << endl;
	}
	// result->repOK();
	//result->print(cout) ;

	// cout<<"* after CAoptim: Problem nodes = "<<result->size()<<endl;

	if (params.olevel >= 4) {
		DagOptim cse(*result, floats);	
		if (params.alterARRACS) {
			cout << " alterARRACS" << endl;
			cse.alterARRACS();
		}
		cse.process(*result);
	}
	// result->repOK();	
	if (params.verbosity > 0) { cout << "* Final Problem size: Problem nodes = " << result->size() << endl; }
	if (params.showDAG) {
		result->lprint(cout);
	}
	if (params.outputMRDAG) {
		ofstream of(params.mrdagfile.c_str());
		cout << "Outputing Machine Readable DAG to file " << params.mrdagfile << endl;
		result->mrprint(of);
		of.close();
	}
	if(params.outputSMT){
		ofstream of(params.smtfile.c_str());
		cout<<"Outputing SMT for DAG to file "<<params.smtfile<<endl;
		result->smtlinprint(of, params.NINPUTS);
		of.close();
	}
    if(params.outputExistsSMT){
		ofstream of(params.smtfile.c_str());
		cout<<"Outputing SMT for DAG to file "<<params.smtfile<<endl;
		result->smt_exists_print(of);
		of.close();
		exit(1);
	}
	return result;
}

bool hasFloatInputs(bool_node* node) {
  //vector<bool_node*> parents = node->parents();
	for (auto it = node->p_begin(); it != node->p_end(); ++it) {
    if ((*it) != NULL && (*it)->getOtype() == OutType::FLOAT) return true;
  }
  return false;
}

bool hasFloatChild(bool_node* node) {
  FastSet<bool_node> children = node->children;
  for(child_iter it = children.begin(); it != children.end(); ++it){
    if ((*it)->getOtype() == OutType::FLOAT) return true;
  }
  return false;
}


void print(set<bool_node*> nodes) {
  set<bool_node*>::iterator it;
  for (it = nodes.begin(); it != nodes.end(); it++) {
    cout << (*it)->lprint() << endl;
  }
}

void InterpreterEnvironment::abstractNumericalPart(BooleanDAG& dag) {
  vector<bool_node*> newnodes;
  set<bool_node*> seenNodes;
  DagOptim op(dag, floats);
  BooleanDAG& dagclone = (*dag.clone());
  vector<OutType*> rettypes;
  string fname = "_GEN_NUM_SYNTH";

  vector<bool_node*> funparents;
  vector<bool_node*> tuplecparents;
  vector<TUPLE_R_node*> trnodes;
  
  BooleanDAG* funDag = new BooleanDAG(fname); // store the abstraction in this dag
  
  set<bool_node*> funNodes;
  
  for(int i=0; i<dag.size() ; ++i ) {
    bool_node* node = dag[i];
    if (seenNodes.find(node) == seenNodes.end()) {
      seenNodes.insert(node);
      if (node == NULL) continue;
      int nid = node->id;
      OutType* type = node->getOtype();
      //cout << dagclone[nid]->lprint() << endl;
      
      
      if (type == OutType::INT || type == OutType::BOOL  ) {
        bool hasFlChild = hasFloatChild(node);
        bool hasFlInputs = hasFloatInputs(dagclone[nid]);
        if (hasFlChild) {
          if (hasFlInputs) {
			  CTRL_node* ctrl =  CTRL_node::create(); // TODO: this ctrl should be angelic
			  ctrl->name = "CTRL_" + std::to_string(seenNodes.size());
          
			  int nbits = 0;
			  if (type == OutType::BOOL || type == OutType::BOOL_ARR) {
				nbits = 1;
			  }
			  if (type == OutType::INT || type == OutType::INT_ARR) {
				nbits = 5;
			  }
          
			  ctrl->set_nbits(nbits);
          
			  if(type == OutType::INT_ARR || type == OutType::BOOL_ARR) {
				ctrl->setArr(PARAMS->angelic_arrsz);
			  }
			  newnodes.push_back(ctrl);
			  funparents.push_back(ctrl);
          } else {
			  funparents.push_back(node);
          }
          
        
      
          SRC_node* src =  SRC_node::create("PARAM_" + std::to_string(seenNodes.size()));
          int nbits = 0;
          if (type == OutType::BOOL || type == OutType::BOOL_ARR) {
            nbits = 1;
          }
          if (type == OutType::INT || type == OutType::INT_ARR) {
            nbits = 2;
          }
          
          if (nbits > 1) { nbits = PARAMS->NANGELICS; }
          src->set_nbits(nbits);
          if(type == OutType::INT_ARR || type == OutType::BOOL_ARR) {
            int sz = 1 << PARAMS->NINPUTS;
            src->setArr(sz);
          }
          
          funNodes.insert(src);
          dagclone[nid]->neighbor_replace(src);
        }
        if (hasFlInputs) {
          funNodes.insert(dagclone[nid]);
		  tuplecparents.push_back(dagclone[nid]);
          TUPLE_R_node* tnode = TUPLE_R_node::create();
          tnode->idx = rettypes.size();                    
		  trnodes.push_back(tnode);
          newnodes.push_back(tnode);
          dag.replace(nid, tnode);
          rettypes.push_back(type);
          if (hasFlChild) {
            EQ_node* eq = EQ_node::create();
            eq->mother() = tnode;
            int sz = funparents.size();
            eq->father() = funparents[sz-1];
            eq->addToParents();
            newnodes.push_back(eq);
            ASSERT_node* an = ASSERT_node::create();
            an->mother() = eq;
            an->addToParents();
            newnodes.push_back(an);
            dag.assertions.append(getDllnode(an));
            
          }
        }
      }
      if (type == OutType::FLOAT) {
        funNodes.insert(dagclone[nid]);
        node->dislodge();
        //dag.replace(nid, NULL);
      }
    }
  }


  TUPLE_CREATE_node* funOutput = TUPLE_CREATE_node::create(tuplecparents);
  funOutput->setName(fname);

  UFUN_node* unode = UFUN_node::create(fname, funparents);
  unode->outname = "_p_out_" + fname;
  unode->set_tupleName(fname);
  unode->set_nbits(0);
  unode->ignoreAsserts = true; // This is ok because the code represented by this ufun has no asserts.  
  unode->mother() = op.getCnode(1);

  for (auto it = trnodes.begin(); it != trnodes.end(); ++it) {
	  (*it)->mother() = unode;
	  (*it)->addToParents();
  }



  vector<bool_node*> v(funNodes.begin(), funNodes.end());
  funDag->addNewNodes(v);
  funOutput->addToParents();
  funDag->addNewNode(funOutput);
  funDag->create_outputs(-1, funOutput);
  funDag->registerOutputs();
  OutType::makeTuple(fname, rettypes, -1);
  unode->addToParents();
  newnodes.push_back(unode);
  funDag->cleanup();
  funDag->cleanup_children();
  numericalAbsMap[fname] = funDag;
  funDag->lprint(cout);
  dag.addNewNodes(newnodes);
  op.cleanup(dag);  
  //dag.lprint(cout);
  //funDag->repOK();
  
  finder->setNumericalAbsMap(numericalAbsMap);
  
}


