/*
 *-----------------------------------------------------------------------------
 * Filename: ias-shell-config.c
 *-----------------------------------------------------------------------------
 * Copyright 2012-2018 Intel Corporation
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
 *   Config file handling for Intel Automotive Solutions shell module
 *-----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compositor.h"
#include "ias-shell.h"

static void handle_hmi(void *, const char **);
static void handle_env(void *, const char **);

/* Element mapping for state machine */
static struct xml_element shell_parse_data[] = {
	{ NONE,			NULL,			NULL,				IASCONFIG,					NONE },
	{ IASCONFIG,	"iasconfig",	NULL,				HMI | PLUGIN | INPUTPLUGIN,	NONE },
	{ HMI,			"hmi",			handle_hmi,			ENV,						IASCONFIG },
	{ ENV,			"hmienv",		handle_env,			NONE,						HMI },
};

/*
 * handle_hmi()
 *
 * Handles an HMI element in the shell config
 */
static void
handle_hmi(void *userdata, const char **attrs)
{
	struct ias_shell *shell = userdata;

	/* Make sure we haven't already seen an HMI element */
	if (shell->hmi.execname) {
		IAS_ERROR("Only one HMI may be specified in the IAS shell config");
		return;
	}

	while (attrs[0]) {
		if (!strcmp(attrs[0], "exec")) {
			shell->hmi.execname = strdup(attrs[1]);
			break;
		} else {
			IAS_ERROR("Unrecognized HMI argument '%s'", attrs[0]);
		}

		attrs += 2;
	}

	/* Initialize the environment list */
	wl_list_init(&shell->hmi.environment);
}

/*
 * handle_env()
 *
 * Handles an environment variable element in the shell config
 */
static void
handle_env(void *userdata, const char **attrs)
{
	struct ias_shell *shell = userdata;
	handle_env_common(attrs, &shell->hmi.environment);
}

void
ias_shell_configuration(struct ias_shell *shell);

/*
 * ias_shell_configuration()
 *
 * Reads the IAS config file to setup shell behavior based on the
 * customer's needs.
 */
void
ias_shell_configuration(struct ias_shell *shell)
{
	ias_read_configuration(CFG_FILENAME, shell_parse_data,
			sizeof(shell_parse_data) / sizeof(shell_parse_data[0]),
			shell);
}
