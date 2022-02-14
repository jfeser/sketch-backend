//
// Created by kliment on 12/20/21.
//

#include "SketchFunction.h"
#include "BooleanDagUtility.h"

#include <utility>

const bool rename_holes = true;

SketchFunction *SketchFunction::produce_concretization(const VarStore &_var_store, const bool_node::Type var_type, const bool do_clone, const bool do_deep_clone) {

    if(_var_store.size() >= 1 && var_type == bool_node::CTRL)
        _var_store.check_rep();

    if(_var_store.size() >= 1 && var_type == bool_node::CTRL) {
        assert(!get_dag()->getNodesByType(bool_node::CTRL).empty());
    }
    else if(_var_store.size() == 0 && var_type == bool_node::CTRL)
    {
        if(get_dag()->getNodesByType(bool_node::UFUN).empty()) {
            assert(get_inlining_tree(false) == nullptr);
            assert(get_dag()->getNodesByType(bool_node::CTRL).empty());
        }
    }

    if(do_clone) {
        assert(do_deep_clone);
        return clone()->produce_concretization(_var_store, var_type, false, do_deep_clone);
    }
    else {

        VarStore var_store = _var_store;

        if(do_deep_clone) {
            deep_clone_tail(var_store);

            if(var_store.size() >= 1 && var_type == bool_node::CTRL)
                var_store.check_rep();

            if (var_store.size() >= 1 && var_type == bool_node::CTRL) {
                InliningTree* tmp_inlining_tree = new InliningTree(this);
                assert(tmp_inlining_tree->match_topology(var_store.get_inlining_tree()));
                tmp_inlining_tree->rename_var_store(var_store);
                var_store.check_rep();
                tmp_inlining_tree->clear();
            }
        }

        if(do_deep_clone && var_type == bool_node::CTRL) {
            calc_inlining_tree();
            get_inlining_tree()->concretize(var_store, var_type);
            InliningTree* inlining_tree_tmp_p = get_inlining_tree();
            get_inlining_tree() = nullptr;
            inlining_tree_tmp_p->clear();
        }

        vector<string> *inlined_functions = nullptr;

        bool prev_has_been_concretized = get_has_been_concretized();
        assert(!prev_has_been_concretized);

        concretize_this_dag(var_store, var_type, inlined_functions);

        assert(inlined_functions != nullptr);

        //construct solution
        if (var_type == bool_node::CTRL && get_has_been_concretized()) {
            AssertDebug(solution == nullptr, "you can't concretize a function twice");
            SATSolverResult sat_solver_result = SAT_UNDETERMINED;
            if (get_dag()->get_failed_assert() != nullptr) {
                sat_solver_result = SAT_UNSATISFIABLE;
            } else if (!get_dag()->getNodesByType(bool_node::CTRL).empty()) {
                sat_solver_result = SAT_NOT_FULLY_CONCRETIZED;
            } else if (get_dag()->get_failed_assert() == nullptr) {
                sat_solver_result = SAT_SATISFIABLE;
            } else {
                assert(false);
            }

            auto compare_solution = new SolverLanguagePrimitives::HoleAssignment(sat_solver_result, &var_store,
                                                                                 get_env()->floats);

            if (solution != nullptr) {
                if (!(*solution == *compare_solution)) {
                    cout << solution->to_string() << endl;
                    cout << compare_solution->to_string() << endl;
                    assert(false);
                }
                compare_solution->clear();
                delete compare_solution;
                get_solution();
            } else {
                solution = compare_solution;
                get_solution();
            }

            get_inlining_tree();

            get_solution();

            assert(get_solution()->get_assignment()->to_var_store()->check_rep());
        }


        rep = get_env()->function_map.concretize(
                get_dag()->get_name(), var_store, var_type, inlined_functions);

        if (var_store.size() >= 1) {
            if (solution->get_sat_solver_result() == SAT_SATISFIABLE) {
                assert(get_dag()->getNodesByType(bool_node::UFUN).empty());
                if (var_store.size() >= 1) assert(get_dag()->getNodesByType(bool_node::CTRL).empty());
            }
        } else {
            assert(get_dag()->getNodesByType(bool_node::UFUN).empty());
        }
        {
            replaced_labels.clear();
            original_labels.clear();
            //TODO: this probably needs to happen, because after concretization there are no ufuns, but not sure why it crashes
//            for (auto dep: responsibility) {
//                dep.second->clear();
//            }
        }

        delete inlined_functions;

        return this;
    }
}

SketchFunction *SketchFunction::clone(const string& explicit_name) {

    assert(rename_holes);
    BooleanDAG* cloned_dag = get_dag()->clone(explicit_name, rename_holes);

    if(rename_holes)
    for(auto it : cloned_dag->getNodesByType(bool_node::CTRL)) {
        string actual_name = ((CTRL_node*)it)->get_name();
        if(actual_name != "#PC") {
            string var_name = ((CTRL_node *) it)->get_original_name();
            string sub_dag_name = ((CTRL_node *) it)->get_source_dag_name();

            assert(sub_dag_name == cloned_dag->get_name());
        }
    }

    auto new_primitive = get_env()->function_map.clone(get_dag()->get_name(), cloned_dag->get_name());

    SolverLanguagePrimitives::HoleAssignment* solution_clone = nullptr;

    if(solution != nullptr) {

        for(auto it : get_dag()->getNodesByType(bool_node::CTRL)) {

            string var_name = ((CTRL_node*)it)->get_original_name();
            string actual_name = ((CTRL_node*)it)->get_name();
            string sub_dag_name = ((CTRL_node*)it)->get_source_dag_name();

            assert(sub_dag_name == dag_name);

            assert(solution->get_assignment()->get_name(var_name, dag_name) == actual_name);
        }

        solution_clone = new SolverLanguagePrimitives::HoleAssignment(solution);

        for(auto it : cloned_dag->getNodesByType(bool_node::CTRL)) {
            string var_name = ((CTRL_node*)it)->get_original_name();
            string actual_name = ((CTRL_node*)it)->get_name();
            string sub_dag_name = ((CTRL_node*)it)->get_source_dag_name();
            assert(sub_dag_name == cloned_dag->get_name());
            solution_clone->rename(actual_name, var_name, sub_dag_name, dag_name, solution->get_assignment()->get_name(var_name, dag_name));
        }
    }
    return new SketchFunction(
            cloned_dag, get_env(), solution_clone, replaced_labels, original_labels, new_primitive, responsibility, get_inlining_tree(false), get_has_been_concretized());
}

void SketchFunction::_clear()
{
    if(local_clear_id != global_clear_id) {
        local_clear_id = global_clear_id;
    }
    else {
//        AssertDebug(false, "WOAH! A CYCLIC DEPENDENCY!! NICE!! (you can remove this now haha).")
        return;
    }

    string dag_name = get_dag()->get_name();

    if(BooleanDagUtility::soft_clear()) {
        get_env()->function_map.erase(dag_name);


        for(auto sk_it : get_env()->function_map)
        {
            auto ufuns = sk_it.second->get_dag()->getNodesByType(bool_node::UFUN);
            for(auto it_ufun : ufuns)
            {
                string ufname = ((UFUN_node*)it_ufun)->get_ufname();
                assert(get_env()->function_map.find(ufname) != get_env()->function_map.end());
            }
        }

        assert(dag_name != "composite_predicate__id104");

        if (solution != nullptr) {
            solution->clear();
            delete solution;
        }

        for(auto it: responsibility) {
            it.second->_clear();
        }

        delete this;
    }
}

void SketchFunction::clear(){
    global_clear_id++;
    _clear();
}

void SketchFunction::replace(const string replace_this, const string with_this) {
    assert(new_way);

    AssertDebug(!is_inlining_tree_nonnull(), "TODO: when renaming, need to update inlining tree.");

    if(replaced_labels.find(replace_this) == replaced_labels.end()) {
        AssertDebug(replaced_labels.find(replace_this) == replaced_labels.end(),
                    "If this happens, it means that you are replacing a label that has previously been replaced (used as 'replace_this'). Not yet handled.");

        assert(replaced_labels.find(replace_this) == replaced_labels.end());

        rep = get_env()->function_map.replace_label_with_another(get_dag()->get_name(), replace_this, with_this);

        assert(replaced_labels.find(replace_this) == replaced_labels.end());

        get_dag()->replace_label_with_another(replace_this, with_this);

        assert(replaced_labels.find(replace_this) == replaced_labels.end());

        auto original_it = original_labels.find(replace_this);
        assert(original_it == original_labels.end());
        original_labels[replace_this] = replace_this;


    } else {
        assert(replaced_labels.find(replace_this) != replaced_labels.end());


        rep = get_env()->function_map.replace_label_with_another(get_dag()->get_name(), replace_this, with_this);
        get_dag()->replace_label_with_another(replaced_labels[replace_this], with_this);

        assert(replaced_labels.find(replace_this) != replaced_labels.end());

        auto original_it = original_labels.find(replace_this);
        assert(original_it != original_labels.end());
        assert(original_it->second == replace_this);
        string prev_dep_name = replaced_labels[replace_this];


        auto dependency_it = get_env()->function_map.find(prev_dep_name);
        assert(dependency_it != get_env()->function_map.end());
        assert(responsibility.find(prev_dep_name) != responsibility.end());
        responsibility[prev_dep_name]->clear();
        responsibility.erase(prev_dep_name);

    }


    auto dependency_it = get_env()->function_map.find(with_this);
    assert(dependency_it != get_env()->function_map.end());
    assert(responsibility.find(with_this) == responsibility.end());
    responsibility[with_this] = dependency_it->second;
    responsibility[with_this]->increment_shared_ptr();


    replaced_labels[replace_this] = with_this;
}

SketchFunction * SketchFunction::produce_get(const string& get_the_dag_under_this_varname) {
//    cout << "in produce get " << get_dag()->get_name() <<" "<< get_the_dag_under_this_varname << endl;
    return get_env()->function_map.produce_get(get_dag()->get_name(), get_the_dag_under_this_varname);
}

bool SketchFunction::solution_is_null() {
    return solution == nullptr;
}

SolverLanguagePrimitives::HoleAssignment *SketchFunction::get_same_soluton() {
    return solution;
}

string SketchFunction::get_assignment(const string& key) {
    auto it = replaced_labels.find(key);
    assert(it != replaced_labels.end());
    return it->second;
}

void SketchFunction::reset(const string& key) {
    auto it = replaced_labels.find(key);
    assert(it != replaced_labels.end());

    auto original_it = original_labels.find(key);
    assert(original_it != original_labels.end());
    assert(original_it->second == key);

    rep = get_env()->function_map.replace_label_with_another(get_dag()->get_name(), key, key);

    replaced_labels.erase(it);
    original_labels.erase(original_it);
}

void SketchFunction::clear_assert_num_shared_ptr_is_0() {
    string dag_name = get_dag()->get_name();

    if(BooleanDagUtility::soft_clear_assert_num_shared_ptr_is_0()) {
        get_env()->function_map.erase(dag_name);
        if (solution != nullptr) {
            solution->clear();
            delete solution;
        }

        delete this;
    }
}

const map<string, string> &SketchFunction::get_replace_map() const {
    return replaced_labels;
}

void SketchFunction::set_rep(const TransformPrimitive *new_or_existing_primitive) {
    if(rep == nullptr) {
        rep = new_or_existing_primitive;
    }
    else {
        assert(rep == new_or_existing_primitive);
    }
}

const TransformPrimitive * SketchFunction::get_rep() {
    return rep;
}

const TransformPrimitive * SketchFunction::get_mirror_rep() const {
    return mirror_rep;
}

void SketchFunction::set_mirror_rep(const TransformPrimitive *_mirror_rep) {
    mirror_rep = _mirror_rep;
}

void SketchFunction::deep_clone_tail(const VarStore& var_store) {

    //first get all inlined functions
    //clone all inlined functions;
    //replace the name inside the inlined function with eachother's

    //assert that all the ufuns are represented in the function map
    for (auto it: get_env()->function_map) {
        for (auto ufun_it: it.second->get_dag()->getNodesByType(bool_node::UFUN)) {
            auto f = get_env()->function_map.find(((UFUN_node *) ufun_it)->get_ufname());
            assert(f != get_env()->function_map.end());
        }
    }


    if(false) {
        vector<string> *inlined_functions = nullptr;
        BooleanDagUtility *to_get_inlined_fs = ((BooleanDagUtility *) this)->clone(true);
        to_get_inlined_fs->increment_shared_ptr();
        to_get_inlined_fs->concretize_this_dag(var_store, bool_node::CTRL, inlined_functions);
        //assert that if the var_store is not empty, then the dag was actually concretized;
        if (var_store.size() >= 1) {
            assert(to_get_inlined_fs->get_dag()->getNodesByType(bool_node::UFUN).empty());
            assert(to_get_inlined_fs->get_dag()->getNodesByType(bool_node::CTRL).empty());
        }
        assert(to_get_inlined_fs->get_dag()->getNodesByType(bool_node::UFUN).empty());
        to_get_inlined_fs->clear();
    }

    InliningTree* tmp_inlining_tree = new InliningTree(this);
    set<string>* inlined_functions = tmp_inlining_tree->get_inlined_function();

    assert(inlined_functions != nullptr);

    //assert that all that the previous operation hasn't corrupted the function map ufuns invariant
    for (auto it: get_env()->function_map) {
        for (auto ufun_it: it.second->get_dag()->getNodesByType(bool_node::UFUN)) {
            auto f = get_env()->function_map.find(((UFUN_node *) ufun_it)->get_ufname());
            assert(f != get_env()->function_map.end());
        }
    }

    // assert that all the dags in the inlined functions have all the ufuns also in the inlined functions
    for (const auto &inlined_f_name: *inlined_functions) {
        auto it_f = get_env()->function_map.find(inlined_f_name);
        assert(it_f != get_env()->function_map.end());
        for (auto ufun_it: it_f->second->get_dag()->getNodesByType(bool_node::UFUN)) {
            auto f = get_env()->function_map.find(((UFUN_node *) ufun_it)->get_ufname());
            assert(f != get_env()->function_map.end());
            assert(inlined_functions->find(((UFUN_node *) ufun_it)->get_ufname()) != inlined_functions->end());
        }
    }

    bool entered_recursive_case = false;
    map<string, SketchFunction *> to_inline_skfuncs;
    for (const string &inlined_function_name: *inlined_functions) {

        if (inlined_function_name != get_dag()->get_name()) {
            assert(to_inline_skfuncs.find(inlined_function_name) == to_inline_skfuncs.end());

            to_inline_skfuncs[inlined_function_name] = get_env()->function_map[inlined_function_name]->clone();
            get_env()->function_map.insert(to_inline_skfuncs[inlined_function_name]->get_dag()->get_name(),
                                           to_inline_skfuncs[inlined_function_name]);
        } else {
            entered_recursive_case = true;
            assert(get_env()->function_map.find(get_dag()->get_name()) != get_env()->function_map.end());
            to_inline_skfuncs[inlined_function_name] = get_env()->function_map[inlined_function_name];
//            AssertDebug(false, "TODO recursive case");
        }
    }


    assert(to_inline_skfuncs.size() == inlined_functions->size());

    for (const auto &it: *inlined_functions) {
        assert(to_inline_skfuncs.find(it) != to_inline_skfuncs.end());
    }

    if (to_inline_skfuncs.find(dag_name) != to_inline_skfuncs.end()) {
        assert(entered_recursive_case);
        assert(to_inline_skfuncs[dag_name] == this);
    } else {
        assert(!entered_recursive_case);
        to_inline_skfuncs[dag_name] = this;
    }

    //rename all the ufuns
    for (auto skfunc: to_inline_skfuncs) {
        if(skfunc.second->get_dag()->get_name() == "composite_predicate__id107__id231")
        {
            cout << "here" << endl;
        }
        set<pair<string, string> > ufnames;

        for (auto ufun_it: skfunc.second->get_dag()->getNodesByType(bool_node::UFUN)) {
            string ufname = ((UFUN_node *) ufun_it)->get_ufname();
            string original = ((UFUN_node *) ufun_it)->get_original_ufname();
            ufnames.insert(make_pair(ufname, original));
        }

        for (const pair<string, string> &ufname_now_original: ufnames) {
            string ufname = ufname_now_original.first;
            string original = ufname_now_original.second;

            if (to_inline_skfuncs.find(ufname) == to_inline_skfuncs.end()) {
                AssertDebug(false, "this should fail here, it was checked before");
            } else {
                skfunc.second->replace(original, to_inline_skfuncs[ufname]->get_dag()->get_name());
            }
        }

        if(skfunc.second->get_dag()->get_name() == "composite_predicate__id107__id231")
        {
            cout << "here" << endl;
        }
    }

    inlined_functions->clear();
    inlined_functions = nullptr;
}


