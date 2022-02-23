#pragma once

#include "BooleanToCNF.h"
#include "FindCheckSolver.h"
#include "BooleanDAG.h"
#include "Tvalue.h"
#include "Checkpointer.h"
#include "VarStore.h"
#include "CommandLineArgs.h"
#include "SolverTypes.h"
#include "HoleHardcoder.h"
#include <stack>
#include <ctime>
#include "FloatSupport.h"
#include "CEGISParams.h"
#include "CounterexampleFinder.h"

using namespace MSsolverNS;

#include "BooleanDagLightUtility.h"


class CEGISChecker
{

	HoleHardcoder& hcoder;
	map<int, vector<VarStore> > expensives;

	bool simulate(VarStore& controls, VarStore& input, vector<VarStore>& expensive);

	lbool baseCheck(VarStore& controls, VarStore& input);

	void setNewControls(VarStore& controls, SolverHelper& dirCheck);
	
	int valueForINode(INTER_node* inode, VarStore& values, int& nbits);

	void growInputs(VarStore & inputStore, BooleanDAG* dag, BooleanDAG* oridag, bool isTop);


	void pushProblem(BooleanDagLightUtility* p){
        p->increment_shared_ptr();
		problemStack.push(p);
	}
	int problemLevel(){
		return (int) problemStack.size();
	}
	void popProblem(){
        BooleanDagLightUtility* t = problemStack.top();
		problemStack.pop();
		t->clear();
	}


	void abstractProblem(VarStore & inputStore, VarStore& ctrlStore);

//--moved from protected;

	stack<BooleanDagLightUtility*> problemStack;

	map<int, File*> files;

	int curProblem; 

	FloatManager& floats;
	CEGISparams params;

	vector<BooleanDagLightUtility*> problems;

	vector<Tvalue> check_node_ids;

    BooleanDAG* check(VarStore& controls, VarStore& input);

    VarStore input_store;

public:

    void clear()
    {
        clear_problemStack();
        assert(problemStack.empty());

        files.clear();

        problems.clear();

        check_node_ids.clear();

    }


    void clear_problemStack()
    {
        while(!problemStack.empty())
        {
            popProblem();
        }
    }

    inline VarStore& get_input_store()
    {
        return input_store;
    }

    BooleanDAG* getProblemDag(){
        assert(!problemStack.empty());
        return problemStack.top()->get_dag();
    }


    BooleanDagLightUtility* getProblem(){
        assert(!problemStack.empty());
        return problemStack.top();
    }

    BooleanDagLightUtility* getHarness()
    {
        assert(!problemStack.empty());
        return problemStack.top();
    }

	CEGISChecker(CommandLineArgs& args,  HoleHardcoder& hc, FloatManager& _floats):
		params(args), floats(_floats), hcoder(hc)
		{}

    BooleanDAG* check(VarStore& controls)
    {
        return check(controls, get_input_store());
    }


	void addProblem(BooleanDagLightUtility *harness, File *file)
	{
		curProblem = (int) problems.size();
		problems.push_back(harness);
        if (file != nullptr) {
            files[curProblem] = file;
        }

        BooleanDagLightUtility* inlined_harness = harness;
        bool new_clone = false;
        if(new_clone) {
            //BE CAREFUL, THIS RENAMES THE SOURCE DAG OF THE HOLES.
            inlined_harness = inlined_harness->produce_inlined_dag();
            inlined_harness->increment_shared_ptr();
            new_clone = true;
        }
        else
        {
            //ASSERT THAT THE DAG WAS ALREADY INLINED.
            //IF THIS FAILS THE DAG WASN'T INLINED.
            if(!inlined_harness->get_dag()->getNodesByType(bool_node::UFUN).empty()) {
                for(auto it: inlined_harness->get_dag()->getNodesByType(bool_node::UFUN)) {
                    string ufname = ((UFUN_node*)it)->get_ufname();
                    assert(inlined_harness->get_env()->function_map.find(ufname) == inlined_harness->get_env()->function_map.end());
                }
            }
            for(auto it:inlined_harness->get_dag()->getNodesByType(bool_node::CTRL)) {
                assert(it->get_name() != "#PC");
            }
        }


        redeclareInputsAndAngelics(get_input_store(), inlined_harness->get_dag());

        // IS THIS DEBUG CODE? YES
        Dout( cout << "problem->get_n_controls() = " << root_dag->get_n_controls() << "  " << root_dag << endl );
        {
            auto problemIn = inlined_harness->get_dag()->getNodesByType(bool_node::CTRL);
            if(PARAMS->verbosity > 2){
                cout<<"  # OF CONTROLS:    "<< problemIn.size() <<endl;
            }
            int cints = 0;
            int cbits = 0;
            int cfloats = 0;
            for(int i=0; i<problemIn.size(); ++i){
                CTRL_node* ctrlnode = dynamic_cast<CTRL_node*>(problemIn[i]);
                if(ctrlnode->getOtype() == OutType::BOOL){
                    cbits++;
                } else if (ctrlnode->getOtype() == OutType::FLOAT) {
                    cfloats++;
                } else{
                    cints++;
                }
            }
            if(PARAMS->verbosity > 2){
                cout<<" control_ints = "<<cints<<" \t control_bits = "<<cbits<< " \t control_floats = " << cfloats <<endl;
            }
        }

        if(new_clone) {
            inlined_harness->clear();
        }
    }


	bool problemStack_is_empty()
	{
		return problemStack.empty();
	}


	vector<Tvalue>& get_check_node_ids()
	{
		return check_node_ids;
	}


};
