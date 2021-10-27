
/*!
 * Copyright (c) 2021 by Contributors
 * \file src/pass/canonicalize_params_for_razor.cc
 * \brief Canonicalize parameters of the backward closure generated by AutoDiff.
 * Specifically, RAZOR will only feed the dy to the backward closure, so other parameters
 * such as in-place updated running mean and variance have to be removed from its parameters.
 */
#include <unordered_map>
#include "mnm/op.h"
#include "mnm/ir.h"
#include "mnm/pass.h"
#include "mnm/binding.h"
#include "meta/src/pass/common.h"
#include "meta/src/pass/let_list.h"

namespace mnm {

namespace pass {
namespace canonicalize_params_for_razor {

using namespace mnm::ir;
using namespace mnm::op;

template <typename T>
using StdSet = std::unordered_set<T, ObjectPtrHash, ObjectPtrEqual>;

/*!
 * \brief Remove the parameters of the backward closure generated by AutoDiff.
 * Input:
 * def @main(...) {
 *   let %fwd_out = (%out, %mean, %var, ...);
 *   let %bwd = fn(%dy: (Tensor, Tensor, ...)) { ... };
 *   let %out = (%fwd_out, %bwd);
 *   %out;
 * }
 * Output:
 * def @main(...) {
 *   let %bwd = fn(%dy: Tensor) { ... };
 *   let %out = (%out, %mean, %var, ..., %bwd);
 * }
 */
class Canonicalizer : public ExprMutator {
 public:
  Canonicalizer() {
  }

  Expr operator()(const Expr& e) {
    auto func = Downcast<Function>(e);
    std::unique_ptr<ExplicitLetList> ell = ExplicitLetList::make(func->body);
    std::vector<Var> vars = ell->vars;
    std::vector<Expr> exprs = ell->exprs;
    size_t n = vars.size();
    CHECK_EQ(vars.size(), exprs.size());

    // Build a var to expression map.
    Map<Var, Expr> var_to_expr;
    for (size_t i = 0; i < n; ++i) {
      var_to_expr.Set(vars[i], exprs[i]);
    }

    auto ret_tuple = exprs[n - 1].as<TupleNode>();
    CHECK_EQ(ret_tuple->fields.size(), 2U)
        << "Expected tuple-2 output (loss, closure), but got " << ret_tuple->fields.size();
    auto fwd_out_var = Downcast<Var>(ret_tuple->fields[0]);
    auto bwd_closure_var = Downcast<Var>(ret_tuple->fields[1]);
    auto bwd_closure = Downcast<Function>(var_to_expr[bwd_closure_var]);
    CHECK_EQ(bwd_closure->params.size(), 1U)
        << "Expected only one parameter in backward closure, but got "
        << bwd_closure->params.size();
    auto bwd_closure_param = bwd_closure->params[0];

    // If the forward output is not a tuple, then do nothing because we do not have
    // in-place updates in this model.
    if (!var_to_expr[fwd_out_var].as<TupleNode>() ||
        bwd_closure_param->checked_type().as<TensorTypeNode>()) {
      return e;
    }

    Expr body = LetList::With([&](LetList* ll) {
      ll_ = ll;
      for (size_t i = 0; i < n - 1; ++i) {
        // Skip the backward closure definition first.
        if (vars[i] != bwd_closure_var) {
          ll->Push(vars[i], exprs[i]);
        }
      }

      // Remove unused parameters in backward closure parameter tuple. They are expected to be
      // the mutation (in-place updating) Vars.
      bwd_closure_param_ = bwd_closure_param;
      auto new_body = Mutate(bwd_closure->body);
      bwd_closure = Function({dy_}, new_body, {}, func->type_params);
      ll->Push(bwd_closure_var, bwd_closure);

      // Flat the forward output tuple.
      Array<Expr> fields;
      for (auto field : var_to_expr[fwd_out_var].as<TupleNode>()->fields) {
        fields.push_back(field);
      }
      fields.push_back(bwd_closure_var);
      ll->Push(vars[n - 1], Tuple(fields));
      return ell->ret;
    });

    return Function(func->params, body, {}, func->type_params);
  }

  Expr VisitExpr_(const TupleGetItemNode* node) {
    // We expect the tuple to be (dy, *mutations), and only the first dy is used by
    // the backward closure, so we should only visit TupleGetItem(tuple, 0) (a.k.a., dy).
    auto tuple = Downcast<Var>(node->tuple);
    if (tuple == bwd_closure_param_) {
      CHECK(!dy_.defined())
          << "More than one element from dy tuple is used, which is not supported yet";
      dy_ = MakeVar("dy", tuple->checked_type().as<TupleTypeNode>()->fields[node->index]);
      return dy_;
    }
    return GetRef<TupleGetItem>(node);
  }

 private:
  LetList* ll_;
  /*! \brief The dy (loss) var used in the backward closure. */
  Var dy_;
  /*! \brief The only backawrd closure parameter, which should be a tuple. */
  Var bwd_closure_param_;
};

}  // namespace canonicalize_params_for_razor

Pass CanonicalizeParamsForRAZOR() {
  return CreateModulePass(
      [=](IRModule mod, const PassContext& pass_ctx) {
        auto entry = ir::Downcast<ir::Function>(mod->Lookup("main"));
        canonicalize_params_for_razor::Canonicalizer canonicalizer;
        ir::BaseFunc updated_entry = ir::Downcast<ir::BaseFunc>(canonicalizer(entry));
        ir::IRModule updated_mod = ir::IRModule(mod->functions);
        updated_mod->Add(updated_mod->GetGlobalVar("main"), updated_entry, true);
        return updated_mod;
      },
      1, "CanonicalizeParamsForRAZOR", {});
}

MNM_REGISTER_GLOBAL("mnm.pass_.CanonicalizeParamsForRAZOR")
    .set_body_typed(CanonicalizeParamsForRAZOR);

}  // namespace pass
}  // namespace mnm
