/*
 *-----------------------------------------------------------------------------
 * Filename: ias-common.c
 *-----------------------------------------------------------------------------
 * Copyright 2014-2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *-----------------------------------------------------------------------------
 * Description:
 *   Intel Automotive Solutions common functions shared by different modules
 *-----------------------------------------------------------------------------
 */

#include "string.h"
#include "ias-common.h"

void handle_env_common(const char **attrs, struct wl_list *list)
{
	struct environment *env;

	env = calloc(1, sizeof *env);
	if (!env) {
		IAS_ERROR("Out of memory while parsing environment configuration");
		return;
	}

	while (attrs[0]) {
		if (!strcmp(attrs[0], "var")) {
			free(env->var);
			env->var = strdup(attrs[1]);
			env->type = ADD;
		} else if (!strcmp(attrs[0], "val")) {
			free(env->val);
			env->val = strdup(attrs[1]);
		} else if (!strcmp(attrs[0], "remove")) {
			free(env->var);
			env->var = strdup(attrs[1]);
			env->type = REMOVE;
		} else {
			IAS_ERROR("Unrecognized environment argument '%s'", attrs[0]);
		}

		attrs += 2;
	}

	if (!env->var || (env->type == ADD && !env->val)) {
		IAS_ERROR("Bad environment setting in configuration");
		free(env->var);
		free(env->val);
		free(env);
		return;
	}

	wl_list_insert(list, &env->link);
}

void set_unset_env(struct wl_list *env)
{
	struct environment *e, *etmp;
	wl_list_for_each_reverse_safe(e, etmp, env, link) {
		if (e->type == ADD) {
			setenv(e->var, e->val, 1);
		} else if (e->type == REMOVE) {
			unsetenv(e->var);
		}

		/* Free environment memory */
		wl_list_remove(&e->link);
		free(e->var);
		free(e->val);
		free(e);
	}

}
