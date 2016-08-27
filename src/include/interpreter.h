/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#ifndef _FR_INTERPRETER_H
#define _FR_INTERPRETER_H
/**
 * $Id$
 *
 * @file include/interpreter.h
 * @brief The outside interface to interpreter.
 *
 * @author Alan DeKok <aland@freeradius.org>
 */
#include <freeradius-devel/conffile.h> /* Need CONF_* definitions */
#include <freeradius-devel/map_proc.h>
#include <freeradius-devel/modpriv.h>
#include <freeradius-devel/rad_assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNLANG_STACK_MAX (64)

/* Actions may be a positive integer (the highest one returned in the group
 * will be returned), or the keyword "return", represented here by
 * MOD_ACTION_RETURN, to cause an immediate return.
 * There's also the keyword "reject", represented here by MOD_ACTION_REJECT
 * to cause an immediate reject. */
#define MOD_ACTION_RETURN  (-1)
#define MOD_ACTION_REJECT  (-2)
#define MOD_PRIORITY_MAX   (64)

/** Types of unlang_t_t nodes
 *
 * Here are our basic types: unlang_t, unlang_group_t, and unlang_module_call_t. For an
 * explanation of what they are all about, see doc/configurable_failover.rst
 */
typedef enum {
	UNLANG_TYPE_NULL = 0,		//!< Modcallable type not set.
	UNLANG_TYPE_MODULE_CALL = 1,	//!< Module method.
	UNLANG_TYPE_GROUP,			//!< Grouping section.
	UNLANG_TYPE_LOAD_BALANCE,		//!< Load balance section.
	UNLANG_TYPE_REDUNDANT_LOAD_BALANCE,//!< Redundant load balance section.
	UNLANG_TYPE_PARALLEL,		//!< execute statements in parallel
#ifdef WITH_UNLANG
	UNLANG_TYPE_IF,			//!< Condition.
	UNLANG_TYPE_ELSE,			//!< !Condition.
	UNLANG_TYPE_ELSIF,			//!< !Condition && Condition.
	UNLANG_TYPE_UPDATE,		//!< Update block.
	UNLANG_TYPE_SWITCH,		//!< Switch section.
	UNLANG_TYPE_CASE,			//!< Case section (within a #UNLANG_TYPE_SWITCH).
	UNLANG_TYPE_FOREACH,		//!< Foreach section.
	UNLANG_TYPE_BREAK,			//!< Break statement (within a #UNLANG_TYPE_FOREACH).
	UNLANG_TYPE_RETURN,		//!< Return statement.
	UNLANG_TYPE_MAP,			//!< Mapping section (like #UNLANG_TYPE_UPDATE, but uses
						//!< values from a #map_proc_t call).
#endif
	UNLANG_TYPE_POLICY,		//!< Policy section.
	UNLANG_TYPE_XLAT,			//!< Bare xlat statement.
	UNLANG_TYPE_RESUME,		//!< where to resume something.
	UNLANG_TYPE_MAX
} unlang_type_t;

/** Returned by #unlang_op_t calls, determine the next action of the interpreter
 *
 * These deal exclusively with control flow.
 */
typedef enum {
	UNLANG_ACTION_CALCULATE_RESULT = 1,	//!< Calculate a new section #rlm_rcode_t value.
	UNLANG_ACTION_CONTINUE,			//!< Execute the next #unlang_t.
	UNLANG_ACTION_PUSHED_CHILD,		//!< #unlang_t pushed a new child onto the stack,
						//!< execute it instead of continuing.
	UNLANG_ACTION_BREAK,			//!< Break out of the current group.
	UNLANG_ACTION_STOP_PROCESSING		//!< Break out of processing the current request (unwind).
} unlang_action_t;

typedef enum {
	UNLANG_GROUP_TYPE_SIMPLE = 0,		//!< Execute each of the children sequentially, until we execute
						//!< all of the children, or one returns #UNLANG_ACTION_BREAK.
	UNLANG_GROUP_TYPE_REDUNDANT,		//!< Execute each of the children until one returns a 'good'
						//!< result i.e. ok, updated, noop, then break out of the group.
	UNLANG_GROUP_TYPE_MAX			//!< Number of group types.
} unlang_group_type_t;

/** A node in a graph of #unlang_op_t (s) that we execute
 *
 * The interpreter acts like a turing machine, with the nodes forming the tape and the
 * #unlang_action_t the instructions.
 *
 * This is the parent 'class' for multiple unlang node specialisations.
 * The #unlang_t struct is listed first in the specialisation so that we can cast between
 * parent/child classes without knowledge of the layout of the structures.
 *
 * The specialisations of the nodes describe additional details of the operation to be performed.
 */
typedef struct unlang_t {
	struct unlang_t	*parent;	//!< Previous node.
	struct unlang_t	*next;		//!< Next node (executed on #UNLANG_ACTION_CONTINUE et al).
	char const		*name;		//!< Unknown...
	char const 		*debug_name;	//!< Printed in log messages when the node is executed.
	unlang_type_t	type;		//!< The specialisation of this node.
	int			actions[RLM_MODULE_NUMCODES];	//!< Priorities for the various return codes.
} unlang_t;

/** Generic representation of a grouping
 *
 * Can represent IF statements, maps, update sections etc...
 */
typedef struct {
	unlang_t		node;		//!< Self.
	unlang_group_type_t	group_type;
	unlang_t		*children;	//!< Children beneath this group.  The body of an if
						//!< section for example.
	unlang_t		*tail;		//!< of the children list.
	CONF_SECTION		*cs;
	int			num_children;

	vp_map_t		*map;		//!< #UNLANG_TYPE_UPDATE, #UNLANG_TYPE_MAP.
	vp_tmpl_t		*vpt;		//!< #UNLANG_TYPE_SWITCH, #UNLANG_TYPE_MAP.
	fr_cond_t		*cond;		//!< #UNLANG_TYPE_IF, #UNLANG_TYPE_ELSIF.

	map_proc_inst_t		*proc_inst;	//!< Instantiation data for #UNLANG_TYPE_MAP.
	bool			done_pass2;
} unlang_group_t;

/** A call to a module method
 *
 */
typedef struct {
	unlang_t		node;		//!< Self.
	module_instance_t	*modinst;	//!< Instance of the module we're calling.
	module_method_t		method;
} unlang_module_call_t;

/** Pushed onto the interpreter stack by a yielding module, indicates the resumption point
 *
 * Unlike normal coroutines in other languages, we represent resumption points as states in a state
 * machine made up of function pointers.
 *
 * When a module yields, it specifies the function to call when whatever condition is
 * required for resumption is satisfied, it also specifies the ctx for that function,
 * which represents the internal state of the module at the time of yielding.
 *
 * If you want normal coroutine behaviour... ctx is arbitrary, and could include a state enum,
 * in which case the function pointer could be the same as the function that yielded, and something
 * like Duff's device could be used to jump back to the yield point.
 *
 * Yield/resume are left as flexible as possible.  Writing async code this way is difficult enough
 * without being straightjacketed.
 */
typedef struct {
	unlang_module_call_t module;	//!< Module call that returned #RLM_MODULE_YIELD.
	fr_unlang_resume_t	callback;	//!< Function the yielding module indicated should
						//!< be called when the request could be resumed.
	fr_unlang_action_t	action_callback;  //!< Function the yielding module indicated should
						//!< be called when the request is poked via an action
	void			*ctx;		//!< Context data for the callback.  Usually represents
						//!< the module's internal state at the time of yielding.
} unlang_resumption_t;

/** A naked xlat
 *
 * @note These are vestigial and may be removed in future.
 */
typedef struct {
	unlang_t		node;		//!< Self.
	int			exec;
	char			*xlat_name;
} unlang_xlat_t;

/** State of a foreach loop
 *
 */
typedef struct {
	vp_cursor_t		cursor;		//!< Used to track our place in the list we're iterating over.
	VALUE_PAIR 		*vps;		//!< List containing the attribute(s) we're iterating over.
	VALUE_PAIR		*variable;	//!< Attribute we update the value of.
	int			depth;		//!< Level of nesting of this foreach loop.
} unlang_stack_entry_foreach_t;

/** State of a redundant operation
 *
 */
typedef struct {
	unlang_t 		*child;
	unlang_t		*found;
} unlang_stack_entry_redundant_t;

/** Our interpreter stack, as distinct from the C stack
 *
 * We don't call the modules recursively.  Instead we iterate over a list of unlang_t and
 * and manage the call stack ourselves.
 *
 * After looking at various green thread implementations, it was decided that using the existing
 * unlang interpreter stack was the best way to perform async I/O.
 *
 * Each request as an unlang interpreter stack associated with it, which represents its progress
 * through the server.  Because the interpreter stack is distinct from the C stack, we can have
 * a single system thread, have many thousands of pending requests
 */
typedef struct {
	rlm_rcode_t		result;
	int			priority;
	unlang_type_t		unwind;		//!< Unwind to this one if it exists.
	bool			do_next_sibling;
	bool			was_if;
	bool			if_taken;
	bool			resume;
	bool			top_frame;
	unlang_t		*unlang;

	union {
		unlang_stack_entry_foreach_t	foreach;
		unlang_stack_entry_redundant_t	redundant;
	};
} unlang_stack_frame_t;

/** An unlang stack associated with a request
 *
 */
typedef struct {
	int			depth;		//!< Current depth we're executing at.
	unlang_stack_frame_t	frame[UNLANG_STACK_MAX];	//!< The stack...
} unlang_stack_t;

typedef unlang_action_t (*unlang_op_func_t)(REQUEST *request, unlang_stack_t *stack,
					    rlm_rcode_t *presult, int *priority);

/** An unlang operation
 *
 * These are like the opcodes in other interpreters.  Each operation, when executed
 * will return an #unlang_action_t, which determines what the interpreter does next.
 */
typedef struct {
	char const		*name;		//!< Name of the operation.
	unlang_op_func_t	func;		//!< Function pointer, that we call to perform the operation.
	bool			children;	//!< Whether the operation can contain children.
} unlang_op_t;

extern unlang_op_t unlang_ops[];

#define MOD_NUM_TYPES (UNLANG_TYPE_XLAT + 1)

extern char const *const comp2str[];

/* Simple conversions: unlang_module_call_t and unlang_group_t are subclasses of unlang_t,
 * so we often want to go back and forth between them. */
static inline unlang_module_call_t *unlang_to_module_call(unlang_t *p)
{
	rad_assert(p->type == UNLANG_TYPE_MODULE_CALL);
	return (unlang_module_call_t *)p;
}

static inline unlang_group_t *unlang_group_to_module_call(unlang_t *p)
{
	rad_assert((p->type > UNLANG_TYPE_MODULE_CALL) && (p->type <= UNLANG_TYPE_POLICY));

	return (unlang_group_t *)p;
}

static inline unlang_t *unlang_module_call_to_node(unlang_module_call_t *p)
{
	return (unlang_t *)p;
}

static inline unlang_t *unlang_group_to_node(unlang_group_t *p)
{
	return (unlang_t *)p;
}

static inline unlang_xlat_t *unlang_to_xlat(unlang_t *p)
{
	rad_assert(p->type == UNLANG_TYPE_XLAT);
	return (unlang_xlat_t *)p;
}

static inline unlang_t *unlang_xlat_to_node(unlang_xlat_t *p)
{
	return (unlang_t *)p;
}

static inline unlang_resumption_t *unlang_to_resumption(unlang_t *p)
{
	rad_assert(p->type == UNLANG_TYPE_RESUME);
	return (unlang_resumption_t *)p;
}

static inline unlang_t *unlang_from_resumption(unlang_resumption_t *p)
{
	return (unlang_t *)p;
}

#ifdef __cplusplus
}
#endif

#endif
