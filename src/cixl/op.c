#include "cixl/bin.h"
#include "cixl/call.h"
#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/eval.h"
#include "cixl/types/fimp.h"
#include "cixl/types/func.h"
#include "cixl/types/lambda.h"
#include "cixl/types/vect.h"
#include "cixl/op.h"
#include "cixl/scope.h"
#include "cixl/tok.h"

struct cx_op_type *cx_op_type_init(struct cx_op_type *type, const char *id) {
  type->id = id;
  type->eval = NULL;
  return type;
}

struct cx_op *cx_op_init(struct cx_op *op, struct cx_op_type *type, size_t tok_idx) {
  op->tok_idx = tok_idx;
  op->type = type;
  return op;
}

static bool begin_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_scope *parent = op->as_begin.child
    ? cx_scope(cx, 0)
    : op->as_begin.parent;
  
  cx_begin(cx, parent);
  return true;
}

cx_op_type(CX_OBEGIN, {
    type.eval = begin_eval;
  });

static bool end_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  if (!op->as_end.push_result) {
    struct cx_scope *s = cx_scope(cx, 0);
    cx_do_vec(&s->stack, struct cx_box, v) { cx_box_deinit(v); }
    cx_vec_clear(&s->stack);
  }
  
  cx_end(cx);
  return true;
}

cx_op_type(CX_OEND, {
    type.eval = end_eval;
  });

static bool cut_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_scope *s = cx_scope(cx, 0);

  if (!s->stack.count) {
    cx_error(cx, cx->row, cx->col, "Nothing to cut");
    return false;
  }

  cx_cut_init(cx_vec_push(&s->cuts), s);
  return true;
}

cx_op_type(CX_OCUT, {
    type.eval = cut_eval;
  });

static bool fimp_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_fimp *imp = op->as_fimp.imp;
  
  if (op->as_fimp.inline1) {
    cx->op += op->as_fimp.num_ops;
    if (!cx_scan_args(cx, imp->func)) { return false; }
    
    if (!cx_fimp_match(imp, &cx_scope(cx, 0)->stack)) {
      cx_error(cx, cx->row, cx->col, "Func not applicable: %s", imp->func->id);
      return false;
    }

    cx_call_init(cx_vec_push(&cx->calls), cx->row, cx->col, imp, cx->op);
    cx->op = op+1;
  } else {
    cx->op += op->as_fimp.num_ops;
  }
  return true;
}

cx_op_type(CX_OFIMP, {
    type.eval = fimp_eval;
  });

static bool fimpdef_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_fimp *imp = op->as_fimpdef.imp;
  imp->scope = cx_scope_ref(cx_scope(cx, 0));
  return true;
}

cx_op_type(CX_OFIMPDEF, {
    type.eval = fimpdef_eval;
  });

static bool funcall_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_func *func = op->as_funcall.func;
  if (!cx_scan_args(cx, func)) { return false; }
  struct cx_scope *s = cx_scope(cx, 0);
  struct cx_fimp *imp = op->as_funcall.imp;

  if (imp) {
    if (!cx_fimp_match(imp, &s->stack)) { imp = NULL; }
  } else {
    imp = op->as_funcall.jit_imp;
    if (imp && !cx_fimp_match(imp, &s->stack)) { imp = NULL; }
    if (!imp) { imp = cx_func_get_imp(func, &s->stack, 0); }
  }
  
  if (!imp) {
    cx_error(cx, cx->row, cx->col, "Func not applicable: %s", func->id);
    return false;
  }
    
  op->as_funcall.jit_imp = imp;

  if (!imp->ptr) {
    struct cx_bin_func *f = cx_bin_get_func(cx->bin, imp);

    if (f) {
      cx_call_init(cx_vec_push(&cx->calls), cx->row, cx->col, imp, cx->op);
      cx->op = cx_vec_get(&cx->bin->ops, f->start_op);
      return true;
    }
  }
  
  return cx_fimp_call(imp, s);
}

cx_op_type(CX_OFUNCALL, {
    type.eval = funcall_eval;
  });

static bool getconst_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_box *v = cx_get_const(cx, op->as_getconst.id, false);
  if (!v) { return false; }
  cx_copy(cx_push(cx_scope(cx, 0)), v);
  return true;
}

cx_op_type(CX_OGETCONST, {
    type.eval = getconst_eval;
  });

static bool getvar_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_scope *s = cx_scope(cx, 0);
  
  if (!op->as_getvar.id.id[0]) {
    if (!s->cuts.count) {
      cx_error(cx, tok->row, tok->col, "Nothing to uncut");
      return false;
    }

    struct cx_cut *c = cx_vec_peek(&s->cuts, 0);
    c->offs--;
    if (!c->offs) { cx_vec_pop(&s->cuts); }
  } else {
    struct cx_box *v = cx_get_var(s, op->as_getvar.id, false);
    if (!v) { return false; }
    cx_copy(cx_push(s), v);
  }
  
  return true;
}

cx_op_type(CX_OGETVAR, {
    type.eval = getvar_eval;
  });

static bool lambda_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_scope *scope = cx_scope(cx, 0);
  struct cx_lambda *l = cx_lambda_new(scope,
				      op->as_lambda.start_op,
				      op->as_lambda.num_ops);
  cx_box_init(cx_push(scope), cx->lambda_type)->as_ptr = l;
  cx->op += l->num_ops;
  return true;
}

cx_op_type(CX_OLAMBDA, {
    type.eval = lambda_eval;
  });

static bool push_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  cx_copy(cx_push(cx_scope(cx, 0)),  &tok->as_box);
  return true;
}

cx_op_type(CX_OPUSH, {
    type.eval = push_eval;
  });

static bool putargs_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_scope *ds = cx_scope(cx, 0), *ss = ds->stack.count ? ds : cx_scope(cx, 1);
  struct cx_fimp *imp = op->as_putargs.imp;
  
  for (struct cx_func_arg *a = cx_vec_peek(&imp->args, 0);
       a >= (struct cx_func_arg *)imp->args.items;
       a--) {
    struct cx_box *src = cx_test(cx_pop(ss, false));

    if (a->id) {
      *cx_put_var(ds, a->sym_id, true) = *src;
    } else {
      cx_box_deinit(src);
    }
  }

  return true;
}

cx_op_type(CX_OPUTARGS, {
    type.eval = putargs_eval;
  });

static bool putvar_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_scope *s = cx_scope(cx, 0);
  struct cx_box *src = cx_pop(s, false);
  
  if (!src) { return false; }

  if (op->as_putvar.type && !cx_is(src->type, op->as_putvar.type)) {
    cx_error(cx, tok->row, tok->col,
	     "Expected type %s, actual: %s",
	     op->as_putvar.type->id, src->type->id);
    return false;
  }
  
  struct cx_box *dst = cx_put_var(s, op->as_putvar.id, true);

  if (!dst) { return false; }
  *dst = *src;
  return true;
}

cx_op_type(CX_OPUTVAR, {
    type.eval = putvar_eval;
  });

static bool return_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_call *call = cx_test(cx_vec_peek(&cx->calls, 0));
  struct cx_fimp *imp = call->target;

  if (call->recalls) {
    if (!cx_scan_args(cx, imp->func)) { return false; }

    if (!cx_fimp_match(imp, &cx_scope(cx, 0)->stack)) {
      cx_error(cx, cx->row, cx->col, "Recall not applicable");
      return false;
    }

    cx->op = cx_vec_get(&cx->bin->ops, op->as_return.start_op+1);
    call->recalls--;
  } else {
    if (call->return_op) {
      cx->op = call->return_op;
    } else {
      cx->stop = true;
    }

    struct cx_scope *ss = cx_scope(cx, 0), *ds = cx_scope(cx, 1);

    if (ss->stack.count < imp->rets.count) {
      cx_error(cx, cx->row, cx->col, "Not enough return values on stack");
      return false;
    }

    if (ss->stack.count > imp->rets.count) {
      cx_error(cx, cx->row, cx->col, "Stack not empty on return");
      return false;
    }

    cx_vec_grow(&ds->stack, ds->stack.count+imp->rets.count);
    size_t i = 0;
    struct cx_func_ret *r = cx_vec_peek(&imp->rets, i);
    
    for (struct cx_box *v = cx_vec_start(&ss->stack);
	 i < ss->stack.count;
	 i++, v++, r++) {
      if (r->type && !cx_is(v->type, r->type)) {
	cx_error(cx, cx->row, cx->col,
		 "Invalid return type.\nExpected %s, actual: %s",
		 r->type->id, v->type->id);

	return false;
      }
      
      *(struct cx_box *)cx_vec_push(&ds->stack) = *v;
    }    

    cx_vec_clear(&ss->stack);
    cx_call_deinit(cx_vec_pop(&cx->calls));
    cx_end(cx);
  }
  
  return true;
}

cx_op_type(CX_ORETURN, {
    type.eval = return_eval;
  });

static bool stash_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  struct cx_scope *s = cx_scope(cx, 0);
  struct cx_vect *v = cx_vect_new();
  v->imp = s->stack;
  cx_vec_init(&s->stack, sizeof(struct cx_box));
  cx_box_init(cx_push(s), s->cx->vect_type)->as_ptr = v;
  return true;
}

cx_op_type(CX_OSTASH, {
    type.eval = stash_eval;
  });

static bool stop_eval(struct cx_op *op, struct cx_tok *tok, struct cx *cx) {
  cx->stop = true;
  return true;
}

cx_op_type(CX_OSTOP, {
    type.eval = stop_eval;
  });
