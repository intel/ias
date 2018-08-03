/*
 *-----------------------------------------------------------------------------
 * Filename: ias-config.c
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
 *   Config file handling for Intel Automotive Solutions backend and
 *   shell modules.
 *-----------------------------------------------------------------------------
 */

#include <assert.h>
#include <expat.h>
#include <stdio.h>
#include <string.h>

#include "config-parser.h"
#include "ias-common.h"

/* General expat handlers */
static void startElement(void *, const char *, const char **);
static void endElement(void *, const char *);

/* Current state machine information being used for parsing */
static struct xml_element *parse_data = NULL;
static int num_elements = 0;

/* Current parsing state */
static int current_state = 0;

/* Expat parsing object */
static XML_Parser parser;


/*
 * ias_read_configuration()
 *
 * Reads the IAS config file to setup backend behavior according to the
 * customer's needs.
 */
int
ias_read_configuration(char *filename,
		struct xml_element *state_machine_def,
		int num,
		void *userdata)
{
	FILE *conf;
	int len;
	int done;
	char buf[BUFSIZ];
	char *cfgfile;

	/* Save the state machine parsing data */
	parse_data = state_machine_def;
	num_elements = num;

	/* Open the config file */
	cfgfile = config_file_path(filename);
	if (!cfgfile) {
		IAS_ERROR("Failed to get generate full path for config filename");
		return -1;
	}

	conf = fopen(cfgfile, "r");
	if (!conf) {
		IAS_ERROR("Failed to open IAS config file (%s): %m", cfgfile);
		free(cfgfile);
		return -1;
	}
	free(cfgfile);

	parser = XML_ParserCreate(NULL);
	if (!parser) {
		IAS_ERROR("Failed to create XML config parser");
		fclose(conf);
		return -1;
	}

	XML_SetUserData(parser, userdata);
	XML_SetElementHandler(parser, startElement, endElement);
	do {
		len = fread(buf, 1, sizeof buf, conf);
		if (ferror(conf)) {
			IAS_ERROR("Failed to read from config file: %m");
			break;
		}
		done = feof(conf);

		if (XML_Parse(parser, buf, len, done) == XML_STATUS_ERROR) {
			IAS_ERROR("Unable to parse IAS config at %s:%lu: %s",
					filename,
					XML_GetCurrentLineNumber(parser),
					XML_ErrorString(XML_GetErrorCode(parser)));
			break;
		}
	} while (!done);
	XML_ParserFree(parser);
	fclose(conf);

	return 0;
}

/*
 * startElement()
 *
 * Begins parsing an XML element in the config file.
 */
static void
startElement(void *userdata, const char *name, const char **attrs)
{
	struct xml_element *curr, *next;
	int i;

	curr = &parse_data[current_state];

	/* Map element back to ID */
	for (i = 0; i < num_elements; i++) {
		next = &parse_data[i];

		if (next->name && strcmp(next->name, name) == 0) {
			/* Found an element we recognize; is it an acceptable child? */
			if (curr->valid_children & next->id) {
				/* Acceptable child; call handler, if any */
				if (next->begin_handler) {
					next->begin_handler(userdata, attrs);
				}

				/* Transition state machine */
				current_state = i;

				return;
			} else {
				IAS_ERROR("Element <%s> found at unexpected location", name);
			}
		}
	}
}

/*
 * endElement()
 *
 * Finishes parsing an XML element in the config file.
 */
static void
endElement(void *userdata, const char *name)
{
	struct xml_element *curr = &parse_data[current_state];
	int i;

	/* Make sure it's the element we were parsing */
	if (curr->name && strcmp(name, curr->name) != 0) {
		return;
	}

	/* Transition state machine */
	for (i = 0; i < num_elements; i++) {
		if (parse_data[i].id == curr->return_to) {
			current_state = i;
			return;
		}
	}
}
