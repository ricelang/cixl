#include "cixl/box.h"
#include "cixl/coro.h"
#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/eval.h"
#include "cixl/func.h"
#include "cixl/scope.h"
#include "cixl/tok.h"

struct cx_coro *cx_coro_init(struct cx_coro *coro,
			     struct cx *cx,
			     struct cx_scope *scope) {
  coro->scope = scope;
  coro->pc = 0;
  coro->nrefs = 1;
  coro->done = false;
  cx_vec_init(&coro->toks, sizeof(struct cx_tok));

  for (size_t i = cx->pc+1; i < cx->toks->count; i++) {
    cx_tok_copy(cx_vec_push(&coro->toks), cx_vec_get(cx->toks, i));
  }
  
  return coro;
}

struct cx_coro *cx_coro_deinit(struct cx_coro *coro) {
  if (coro->scope) { cx_scope_unref(coro->scope); }

  cx_do_vec(&coro->toks, struct cx_tok, t) { cx_tok_deinit(t); }
  cx_vec_deinit(&coro->toks);

  return coro;
}

static void yield_imp(struct cx_scope *scope) {
  struct cx *cx = scope->cx;
  
  if (cx->coro) {
    cx->coro->pc = cx->pc+1;    
  } else {
    struct cx_coro *coro = cx_coro_init(malloc(sizeof(struct cx_coro)),
					cx,
					cx_pop_scope(cx, true));

    cx_box_init(cx_push(cx_scope(cx, 0)), cx->coro_type)->as_coro = coro;
  }
  
  cx->stop_pc = cx->pc+1;
}

static bool call(struct cx_box *value, struct cx_scope *scope) {
  struct cx *cx = scope->cx;
  struct cx_coro *coro = value->as_coro;
  
  if (coro->done) {
    cx_error(cx, cx->row, cx->col, "Coro is done");
    return false;
  }
    
  if (coro->scope) {
    cx_push_scope(cx, coro->scope);
  } else {
    cx_push_scope(cx, cx->main);
  }
  
  cx->coro = coro;
  bool ok = cx_eval(cx, &coro->toks, coro->pc);
  cx->coro = NULL;
  if (!ok) { return false; }
  coro->pc = cx->pc;
  if (coro->pc == coro->toks.count) { coro->done = true; }
  cx_pop_scope(cx, false);
  return true;
}

static void fprint(struct cx_box *value, FILE *out) { 
  fprintf(out, "Coro(%p:%d)", value->as_coro, value->as_coro->nrefs);
}

static void copy(struct cx_box *dst, struct cx_box *src) {
  struct cx_coro *c = src->as_coro;
  dst->as_coro = c;
  c->nrefs++;
}

static void deinit(struct cx_box *value) {
  struct cx_coro *coro = value->as_coro;
  cx_ok(coro->nrefs > 0);
  coro->nrefs--;
  if (!coro->nrefs) { free(cx_coro_deinit(coro)); }
}

struct cx_type *cx_init_coro_type(struct cx *cx) {
  struct cx_type *t = cx_add_type(cx, "Coro", cx->any_type, NULL);
  t->fprint = fprint;
  t->call = call;
  t->copy = copy;
  t->deinit = deinit;

  cx_add_func(cx, "yield")->ptr = yield_imp;

  return t;
}
