//
// Created by kliment on 12/19/21.
//

#include "SolverLanguageLexAndYaccHeader.h"
#include "SolverLanguageYaccHeader.h"
#include "SketchFunction.h"
#include "File.h"
#include "SolverLanguage.h"

void SL::Var::run(SolverProgramState *state)  {
    state->add_var(this);
}

SL::VarVal *SL::Var::eval(SolverProgramState *state) {
    return state->get_var_val(this);
}

SL::Name *SL::Var::get_type() {
    return type;
}

void SL::While::run(SolverProgramState* state)
{
    while(predicate->eval(state))
    {
        body->run(state);
    }
}
void SL::For::run(SolverProgramState* state)
{
    def->run(state);
    while(predicate->eval(state))
    {
        body->run(state);
        plus_plus->run(state);
    }
}

int SL::Operand::eval(SolverProgramState* state)
{
    switch (meta_type) {
        case var_operand:
            return state->get_var_val(state->name_to_var(name))->get_int();
            break;
        case predicate_operand:
            return predicate->eval(state);
            break;
        default:
            assert(false);
    }
}

void SL::If::run(SolverProgramState *state) {
    if(predicate->eval(state))
    {
        body->run(state);
    }
}

void SL::Return::run(SolverProgramState *state)
{
    state->set_return_var_val(state->name_to_var(name)->eval(state));
}

void SL::Assignment::run(SolverProgramState *state)
{
    Var* to_var = nullptr;
    switch (dest_type) {
        case name_dest_type:
            to_var = state->name_to_var(dest_name);
            break;
        case var_dest_type:
            to_var = dest_var;
            state->add_var(to_var);
            break;
        default:
            assert(false);
    }
    assert(to_var != nullptr);

    SL::VarVal* var_val = nullptr;
    switch (src_type) {
        case func_call_src_type:
            var_val = func_call->eval(state);
            break;
        case const_src_type:
            var_val = my_const->eval(state);
            break;
        case name_src_type:
            var_val = from_name->eval(state);
            break;
        case no_src_type:
            var_val = nullptr;
            break;
    }

    if(var_val != nullptr) {
        state->set_var_val(to_var, var_val);
    }
    else
    {
        assert(dest_type == var_dest_type);;
//        state->add_var_name(to_var);
    }
}

bool SL::Assignment::has_assignment() {
    return src_type != no_src_type;
}


SL::VarVal *SL::Name::eval(SolverProgramState *state)  {
    return state->get_var_val(state->name_to_var(this));
}



SL::VarVal *SL::FuncCall::eval(SolverProgramState *state)
{
    enum PredefMethod {
        no_predef, predef_file, produce_subset_file,
        sat_solver, concretize, size,
        get, passes, plus,
        clear, Solution, join,
        print, num_holes, vector_pair_int_solution,
        emplace_back, make_pair, first, div, mult, to_float, sort_vec};

    PredefMethod predef_method = no_predef;

    string method_str = method_name->to_string();

    if(method_str == "File")
    {
        predef_method = predef_file;
    }
    else if(method_str == "produce_subset_file")
    {
        predef_method = produce_subset_file;
    }
    else if(method_str == "SATSolver")
    {
        predef_method = sat_solver;
    }
    else if(method_str == "concretize")
    {
        predef_method = concretize;
    }
    else if(method_str == "size")
    {
        predef_method = size;
    }
    else if(method_str == "get")
    {
        predef_method = get;
    }
    else if(method_str == "passes")
    {
        predef_method = passes;
    }
    else if(method_str == "plus")
    {
        predef_method = plus;
    }
    else if(method_str == "clear")
    {
        predef_method = clear;
    }
    else if(method_str == "Solution")
    {
        predef_method = Solution;
    }
    else if(method_str == "join")
    {
        predef_method = join;
    }
    else if(method_str == "print")
    {
        predef_method = print;
    }
    else if(method_str == "num_holes")
    {
        predef_method = num_holes;
    }
    else if(method_str == "emplace_back")
    {
        predef_method = emplace_back;
    }
    else if(method_str == "vector_pair_int_solution")
    {
        predef_method = vector_pair_int_solution;
    }
    else if(method_str == "make_pair")
    {
        predef_method = make_pair;
    }
    else if(method_str == "first")
    {
        predef_method = first;
    }
    else if(method_str == "div")
    {
        predef_method = div;
    }
    else if(method_str == "mult")
    {
        predef_method = mult;
    }
    else if(method_str == "float")
    {
        predef_method = to_float;
    }
    else if(method_str == "sort")
    {
        predef_method = sort_vec;
    }

    switch (predef_method) {
        case predef_file:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "namespace");
            assert(params.size() == 2);
            string file_name = params[0]->eval(state)->get_string();
            SketchFunction* harness = params[1]->eval(state)->get_harness();
            return new SL::VarVal(new File(harness->clone()->do_inline()->get_dag(), file_name, state->floats, state->args.seed));
            break;
        }
        case produce_subset_file:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "File");
            assert(params.size() == 1);
            int num_rows = params[0]->eval(state)->get_int();
            File* file = state->get_var_val(state->name_to_var(var_name))->get_file();
            return new SL::VarVal(file->sample_sub_file(num_rows));
            break;
        }
        case sat_solver:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "namespace");
            assert(params.size() == 2);
            SketchFunction* harness = params[0]->eval(state)->get_harness()->produce_inlined_dag();
            cout << "NUM CTRLS HEERE: "<< harness->get_dag()->getNodesByType(bool_node::CTRL).size() << endl;
            File* file = params[1]->eval(state)->get_file();
            using namespace SolverLanguagePrimitives;
            WrapperAssertDAG* solver =
                    new WrapperAssertDAG(state->floats, state->hc, state->args, state->hasGoodEnoughSolution);
            SolutionHolder* sol = (solver)->
                    solve(new ProblemAE(harness, file));
            harness->clear();
            return new SL::VarVal(sol);
            break;
        }
        case size:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "File" || var_type_str == "vector_pair_int_solution");
            assert(params.empty());
            if(var_type_str == "File") {
                File *file = state->get_var_val(var)->get_file();
                return new SL::VarVal((int) file->size());
            } else if(var_type_str == "vector_pair_int_solution") {
                vector<pair<int, SolverLanguagePrimitives::SolutionHolder*> >* vec = state->get_var_val(var)->get_vector();
                return new SL::VarVal((int) vec->size());
            } else
            {
                assert(false);
            }
            break;
        }
        case concretize:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "SketchFunction");
            assert(params.size() == 1);
            using namespace SolverLanguagePrimitives;
            SolutionHolder* sol = params[0]->eval(state)->get_solution();
            SketchFunction* harness = state->get_var_val(var)->get_harness();
            SketchFunction* concretized_function =
                    harness->produce_with_concretized_holes(sol, true);
//            SketchFunction* concretized_function = harness->concretize(
//                    sol, bool_node::CTRL, true);
            return new SL::VarVal(concretized_function);
            break;
        }
        case get:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "File" || var_type_str == "vector_pair_int_solution");
            assert(params.size() == 1);
            if(var_type_str == "File") {
                assert(params.size() == 1);
                File* file = state->get_var_val(var)->get_file();
                int row_id = params[0]->eval(state)->get_int();
                return new SL::VarVal(new SolverLanguagePrimitives::InputHolder(file->at(row_id), state->floats));
            } else if(var_type_str == "vector_pair_int_solution") {
                vector<pair<int, SolverLanguagePrimitives::SolutionHolder*> >* vec = state->get_var_val(var)->get_vector();
                int row_id = params[0]->eval(state)->get_int();
                return new SL::VarVal(vec->at(row_id));
            } else
            {
                assert(false);
            }
            break;
        }
        case passes:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "SketchFunction");
            assert(params.size() == 1);
            SketchFunction* program = state->get_var_val(var)->get_function();
            SolverLanguagePrimitives::InputHolder* input_holder = params[0]->eval(state)->get_input_holder();
            SketchFunction* concretized_function = program->produce_with_concretized_inputs(input_holder);
            assert((concretized_function->get_dag()->size() == 0) == (concretized_function->get_dag()->get_failed_assert() == nullptr));
            bool ret = concretized_function->get_dag()->get_failed_assert() == nullptr;
            concretized_function->clear();
            return new VarVal(ret);
            break;
        }
        case plus:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "namespace");
            assert(params.size() == 2);
            int left_op = params[0]->eval(state)->get_int();
            int right_op = params[1]->eval(state)->get_int();
            return new VarVal(left_op+right_op);
            break;
        }
        case div:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "namespace");
            assert(params.size() == 2);
            float left_op = params[0]->eval(state)->get_float();
            float right_op = params[1]->eval(state)->get_float();
            return new VarVal(left_op/right_op);
            break;
        }
        case mult:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "namespace");
            assert(params.size() == 2);
            float left_op = params[0]->eval(state)->get_float();
            float right_op = params[1]->eval(state)->get_float();
            return new VarVal(left_op*right_op);
            break;
        }
        case clear:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "SketchFunction");
            assert(params.empty());
            SketchFunction* program = state->get_var_val(var)->get_function();
            program->clear();
            return new VarVal();
            break;
        }
        case Solution:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "namespace");
            assert(params.size() == 0);
            using namespace SolverLanguagePrimitives;
            return new VarVal(new SolutionHolder(false));
        }
        case join:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "Solution");
            assert(params.size() == 1);
            using namespace SolverLanguagePrimitives;
            SolutionHolder* other_solution = params[0]->eval(state)->get_solution();
            SolutionHolder* base_solution = state->get_var_val(var)->get_solution();
            base_solution->join_with(other_solution);
            return new VarVal();
        }
        case print:
        {
            Var* var = state->name_to_var(var_name);
            string var_type_str = var->get_type()->to_string();
            assert(var_type_str == "namespace");
            assert(!params.empty());
            string print_str = "";
            cout << "SOLVER_PROGRAM_PRINT INIT" << endl;
            for(int i = 0;i<params.size();i++)
            {
                cout << params[i]->eval(state)->to_string();
            }
            cout << endl << "SOLVER_PROGRAM_PRINT END" << endl;
            return new VarVal();
        }
        case num_holes: {
            Var *var = get_var_assert_type(state, "SketchFunction");
            assert(params.empty());
            SketchFunction* func_clone = state->get_var_val(var)->get_harness()->clone()->do_inline();
            int num_ctrls = (int) func_clone->get_dag()->getNodesByType(bool_node::CTRL).size();
            func_clone->clear();
            return new VarVal(num_ctrls);
            break;
        }
        case emplace_back:
        {
            Var *var = get_var_assert_type(state, "vector_pair_int_solution");
            assert(params.size() == 1);
            using namespace SolverLanguagePrimitives;
            vector<pair<int, SolutionHolder*> >* vec = state->get_var_val(var)->get_vector();
            pair<int, SolutionHolder*> pair_param = params[0]->eval(state)->get_pair();
            vec->emplace_back(pair_param);
            return new VarVal();
            break;
        }
        case vector_pair_int_solution:
        {
            Var *var = get_var_assert_type(state, "namespace");
            assert(params.empty());
            using namespace SolverLanguagePrimitives;
            vector<pair<int, SolutionHolder*> >* vec = new vector<pair<int, SolutionHolder*> >();
            return new VarVal(vec);
            break;
        }
        case make_pair:
        {
            Var *var = get_var_assert_type(state, "namespace");
            assert(params.size() == 2);
            using namespace SolverLanguagePrimitives;
            int the_int = params[0]->eval(state)->get_int();
            SolverLanguagePrimitives::SolutionHolder* the_solution = params[1]->eval(state)->get_solution();
            return new VarVal(pair<int, SolverLanguagePrimitives::SolutionHolder*>(the_int, the_solution));
            break;
        }
        case first:
        {
            Var *var = get_var_assert_type(state, "pair_int_solution");
            assert(params.size() == 0);
            using namespace SolverLanguagePrimitives;
            pair<int, SolverLanguagePrimitives::SolutionHolder*> the_pair = state->get_var_val(var)->get_pair();
            return new VarVal(the_pair.first);
            break;
        }
        case to_float:
        {
            Var *var = get_var_assert_type(state, "namespace");
            assert(params.size() == 1);
            float the_float = (float)params[0]->eval(state)->get_int();
            return new VarVal(the_float);
            break;
        }
        case sort_vec:
        {
            Var *var = get_var_assert_type(state, "vector_pair_int_solution");
            assert(params.empty());
            using namespace SolverLanguagePrimitives;
            vector<pair<int, SolutionHolder*> >* vec = state->get_var_val(var)->get_vector();
            sort(vec->begin(), vec->end());
            return new VarVal();
            break;
        }
        case no_predef:
            return state->get_method(state->method_name_to_var(method_name))->eval(state, params);
            break;
        default:
            assert(false);
    }
    return nullptr;
}

void SL::FuncCall::run(SolverProgramState *state) {
    SL::VarVal* ret = eval(state);
    assert(ret->is_void());
}

SL::Var *SL::FuncCall::get_var_assert_type(SolverProgramState* state, string type_name) {

    Var* var = state->name_to_var(var_name);
    string var_type_str = var->get_type()->to_string();
    assert(var_type_str == type_name);

    return var;
}

SL::VarVal *SL::Method::eval(SolverProgramState *state, vector<Param*>& input_params)  {
    run(state, input_params);
    return state->get_return_var_val();
}

void SL::Method::run(SolverProgramState *state, vector<Param *> &input_params)  {
    assert(var != nullptr);
    assert(body != nullptr);

    state->new_stack_frame(params, input_params);

    body->run(state);

    state->pop_stack_frame();

}

SL::VarVal *SL::Param::eval(SolverProgramState *state)  {
    switch (meta_type) {
        case is_name:
            return state->get_var_val(state->name_to_var(name));
            break;
        case is_var:
            return state->get_var_val(var);
            break;
        case is_var_val:
            return var_val;
            break;
        case is_func_call:
            return func_call->eval(state);
            break;
        default:
            assert(false);
    }
}

void SL::Methods::populate_state(Frame &frame)  {
    Methods* at = this;
    while(at != nullptr)
    {
        frame.add_var_and_set_var_val(at->head->get_var(), new SL::VarVal(at->head));
        at = at->rest;
    }
}
