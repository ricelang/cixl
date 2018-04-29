#include "cixl/arg.h"
#include "cixl/call.h"
#include "cixl/cx.h"
#include "cixl/error.h"
#include "cixl/lib.h"
#include "cixl/lib/gfx.h"
#include "cixl/scope.h"
#include "cixl/str.h"

static bool new_rgb_imp(struct cx_call *call) {
  struct cx_box
    *r = cx_test(cx_call_arg(call, 0)),
    *g = cx_test(cx_call_arg(call, 1)),
    *b = cx_test(cx_call_arg(call, 2));

  struct cx_scope *s = call->scope;

  cx_rgb_init(&cx_box_init(cx_push(s), s->cx->rgb_type)->as_rgb,
	      r->as_int,
	      g->as_int,
	      b->as_int);
  
  return true;
}

static bool rgb_float_scale_imp(struct cx_call *call) {
  struct cx_rgb *in = &cx_test(cx_call_arg(call, 0))->as_rgb;
  struct cx_box *f = cx_test(cx_call_arg(call, 1));
  struct cx_scope *s = call->scope;
  struct cx_rgb *out = &cx_box_init(cx_push(s), s->cx->rgb_type)->as_rgb;
  out->r = cx_max(cx_min(in->r*f->as_float, 255), 0);
  out->g = cx_max(cx_min(in->g*f->as_float, 255), 0);
  out->b = cx_max(cx_min(in->b*f->as_float, 255), 0);
  return true;
}

cx_lib(cx_init_gfx, "cx/gfx") {    
  struct cx *cx = lib->cx;
    
  if (!cx_use(cx, "cx/abc", "A", "Str")) {
    return false;
  }

  cx->color_type = cx_add_type(lib, "Color", cx->any_type);
  cx->color_type->meta = CX_TYPE_ID;

  cx->rgb_type = cx_init_rgb_type(lib);
  
  cx_add_cfunc(lib, "new-rgb",
	       cx_args(cx_arg("r", cx->int_type),
		       cx_arg("g", cx->int_type),
		       cx_arg("b", cx->int_type)),
	       cx_args(cx_arg(NULL, cx->rgb_type)),
	       new_rgb_imp);

  cx_add_cfunc(lib, "*",
	       cx_args(cx_arg("c", cx->rgb_type),
		       cx_arg("f", cx->float_type)),
	       cx_args(cx_arg(NULL, cx->rgb_type)),
	       rgb_float_scale_imp);

  return true;
}
