/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <expat.h>


#include "wayland-util.h"

void usage(void)
{
	fprintf(stderr, "usage: ./scanner input_XML_file");
	fprintf(stderr, "\n");
	fprintf(stderr, "Converts XML protocol descriptions supplied on "
			"input arguement to HTML \n");
	exit(-1);
}

#define XML_BUFFER_SIZE 4096

struct location {
	const char *filename;
	int line_number;
};

struct description {
	char *summary;
	char *text;
};

struct protocol {
	char *name;
	char *uppercase_name;
	struct wl_list interface_list;
	int type_index;
	int null_run_length;
	char *copyright;
        char *summary;
	struct description *description;
};

struct interface {
	struct location loc;
	char *name;
	char *uppercase_name;
        char *summary;
	int version;
	int since;
	struct wl_list request_list;
	struct wl_list event_list;
	struct wl_list enumeration_list;
	struct wl_list link;
	struct description *description;
};

struct message {
	struct location loc;
	char *name;
	char *uppercase_name;
	struct wl_list arg_list;
	struct wl_list link;
	int arg_count;
	int new_id_count;
	int type_index;
	int all_null;
	int destructor;
	int since;
	struct description *description;
};

enum arg_type {
	NEW_ID,
	INT,
	UNSIGNED,
	FIXED,
	STRING,
	OBJECT,
	ARRAY,
	FD
};

struct arg {
	char *name;
	enum arg_type type;
	int nullable;
	char *interface_name;
	struct wl_list link;
	char *summary;
};

struct enumeration {
	char *name;
	char *uppercase_name;
	struct wl_list entry_list;
	struct wl_list link;
	struct description *description;
};

struct entry {
	char *name;
	char *uppercase_name;
	char *value;
	char *summary;
	struct wl_list link;
};

struct parse_context {
	struct location loc;
	XML_Parser parser;
	struct protocol *protocol;
	struct interface *interface;
	struct message *message;
	struct enumeration *enumeration;
	struct description *description;
	char character_data[8192];
	unsigned int character_data_length;
};

static void *
fail_on_null(void *p)
{
	if (p == NULL) {
		fprintf(stderr, "wayland-scanner: out of memory\n");
		exit(EXIT_FAILURE);
	}

	return p;
}

static void *
xmalloc(size_t s)
{
	return fail_on_null(malloc(s));
}

static char *
xstrdup(const char *s)
{
	return fail_on_null(strdup(s));
}

static char *
uppercase_dup(const char *src)
{
	char *u;
	int i;

	u = xstrdup(src);
	for (i = 0; u[i]; i++)
		u[i] = toupper(u[i]);
	u[i] = '\0';

	return u;
}

static const char *indent(int n)
{
	const char *whitespace[] = {
		"\t\t\t\t\t\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t\t\t\t\t ",
		"\t\t\t\t\t\t\t\t\t\t\t\t  ",
		"\t\t\t\t\t\t\t\t\t\t\t\t   ",
		"\t\t\t\t\t\t\t\t\t\t\t\t    ",
		"\t\t\t\t\t\t\t\t\t\t\t\t     ",
		"\t\t\t\t\t\t\t\t\t\t\t\t      ",
		"\t\t\t\t\t\t\t\t\t\t\t\t       "
	};

	return whitespace[n % 8] + 12 - n / 8;
}

static void
desc_dump(char *desc, const char *fmt, ...) __attribute__((format(printf,2,3)));

static void
desc_dump(char *desc, const char *fmt, ...)
{
	va_list ap;
	char buf[128], hang;
	int col, i, j, k, startcol, newlines;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	for (i = 0, col = 0; buf[i] != '*'; i++) {
		if (buf[i] == '\t')
			col = (col + 8) & ~7;
		else
			col++;
	}

	printf("%s", buf);

	if (!desc) {
		printf("(none)\n");
		return;
	}

	startcol = col;
	col += strlen(&buf[i]);
	if (col - startcol > 2)
		hang = '\t';
	else
		hang = ' ';

	for (i = 0; desc[i]; ) {
		k = i;
		newlines = 0;
		while (desc[i] && isspace(desc[i])) {
			if (desc[i] == '\n')
				newlines++;
			i++;
		}
		if (!desc[i])
			break;

		j = i;
		while (desc[i] && !isspace(desc[i]))
			i++;

		if (newlines > 1)
			printf("\n%s*", indent(startcol));
		if (newlines > 1 || col + i - j > 72) {
			printf("\n%s*%c", indent(startcol), hang);
			col = startcol;
		}

		if (col > startcol && k > 0)
			col += printf(" ");
		col += printf("%.*s", i - j, &desc[j]);
	}
	putchar('\n');
}

static void
fail(struct location *loc, const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	fprintf(stderr, "%s:%d: error: ",
		loc->filename, loc->line_number);
	vfprintf(stderr, msg, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void
warn(struct location *loc, const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	fprintf(stderr, "%s:%d: warning: ",
		loc->filename, loc->line_number);
	vfprintf(stderr, msg, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

static int
is_nullable_type(struct arg *arg)
{
	switch (arg->type) {
	/* Strings, objects, and arrays are possibly nullable */
	case STRING:
	case OBJECT:
	case NEW_ID:
	case ARRAY:
		return 1;
	default:
		return 0;
	}
}

static void
start_element(void *data, const char *element_name, const char **atts)
{
	struct parse_context *ctx = data;
	struct interface *interface;
	struct message *message;
	struct arg *arg;
	struct enumeration *enumeration;
	struct entry *entry;
	struct description *description;
	const char *name, *type, *interface_name, *value, *summary, *since;
	const char *allow_null;
	char *end;
	int i, version;

	ctx->loc.line_number = XML_GetCurrentLineNumber(ctx->parser);
	name = NULL;
	type = NULL;
	version = 0;
	interface_name = NULL;
	value = NULL;
	summary = NULL;
	description = NULL;
	since = NULL;
	allow_null = NULL;
	for (i = 0; atts[i]; i += 2) {
		if (strcmp(atts[i], "name") == 0)
			name = atts[i + 1];
		else if (strcmp(atts[i], "version") == 0)
			version = atoi(atts[i + 1]);
		else if (strcmp(atts[i], "type") == 0)
			type = atts[i + 1];
		else if (strcmp(atts[i], "value") == 0)
			value = atts[i + 1];
		else if (strcmp(atts[i], "interface") == 0)
			interface_name = atts[i + 1];
		else if (strcmp(atts[i], "summary") == 0)
			summary = atts[i + 1];
		else if (strcmp(atts[i], "since") == 0)
			since = atts[i + 1];
		else if (strcmp(atts[i], "allow-null") == 0)
			allow_null = atts[i + 1];
	}

	ctx->character_data_length = 0;
	if (strcmp(element_name, "protocol") == 0) {
		if (name == NULL)
			fail(&ctx->loc, "no protocol name given");

		ctx->protocol->name = xstrdup(name);
		ctx->protocol->uppercase_name = uppercase_dup(name);
		ctx->protocol->description = NULL;
	} else if (strcmp(element_name, "copyright") == 0) {
		
	} else if (strcmp(element_name, "interface") == 0) {
		if (name == NULL)
			fail(&ctx->loc, "no interface name given");

		if (version == 0)
			fail(&ctx->loc, "no interface version given");

		interface = xmalloc(sizeof *interface);
		interface->loc = ctx->loc;
		interface->name = xstrdup(name);
		interface->uppercase_name = uppercase_dup(name);
		interface->version = version;
		interface->description = NULL;
		interface->since = 1;
		wl_list_init(&interface->request_list);
		wl_list_init(&interface->event_list);
		wl_list_init(&interface->enumeration_list);
		wl_list_insert(ctx->protocol->interface_list.prev,
			       &interface->link);
		ctx->interface = interface;
	} else if (strcmp(element_name, "request") == 0 ||
		   strcmp(element_name, "event") == 0) {
		if (name == NULL)
			fail(&ctx->loc, "no request name given");

		message = xmalloc(sizeof *message);
		message->loc = ctx->loc;
		message->name = xstrdup(name);
		message->uppercase_name = uppercase_dup(name);
		wl_list_init(&message->arg_list);
		message->arg_count = 0;
		message->new_id_count = 0;
		message->description = NULL;

		if (strcmp(element_name, "request") == 0)
			wl_list_insert(ctx->interface->request_list.prev,
				       &message->link);
		else
			wl_list_insert(ctx->interface->event_list.prev,
				       &message->link);

		if (type != NULL && strcmp(type, "destructor") == 0)
			message->destructor = 1;
		else
			message->destructor = 0;

		if (since != NULL) {
			errno = 0;
			version = strtol(since, &end, 0);
			if (errno == EINVAL || end == since || *end != '\0')
				fail(&ctx->loc,
				     "invalid integer (%s)\n", since);
		} else {
			version = 1;
		}

		if (version < ctx->interface->since)
			warn(&ctx->loc, "since version not increasing\n");
		ctx->interface->since = version;
		message->since = version;

		if (strcmp(name, "destroy") == 0 && !message->destructor)
			fail(&ctx->loc, "destroy request should be destructor type");

		ctx->message = message;
	} else if (strcmp(element_name, "arg") == 0) {
		if (name == NULL)
			fail(&ctx->loc, "no argument name given");

		arg = xmalloc(sizeof *arg);
		arg->name = xstrdup(name);

		if (strcmp(type, "int") == 0)
			arg->type = INT;
		else if (strcmp(type, "uint") == 0)
			arg->type = UNSIGNED;
		else if (strcmp(type, "fixed") == 0)
			arg->type = FIXED;
		else if (strcmp(type, "string") == 0)
			arg->type = STRING;
		else if (strcmp(type, "array") == 0)
			arg->type = ARRAY;
		else if (strcmp(type, "fd") == 0)
			arg->type = FD;
		else if (strcmp(type, "new_id") == 0) {
			arg->type = NEW_ID;
		} else if (strcmp(type, "object") == 0) {
			arg->type = OBJECT;
		} else {
			fail(&ctx->loc, "unknown type (%s)", type);
		}

		switch (arg->type) {
		case NEW_ID:
			ctx->message->new_id_count++;

			/* Fall through to OBJECT case. */

		case OBJECT:
			if (interface_name)
				arg->interface_name = xstrdup(interface_name);
			else
				arg->interface_name = NULL;
			break;
		default:
			if (interface_name != NULL)
				fail(&ctx->loc, "interface attribute not allowed for type %s", type);
			break;
		}

		if (allow_null == NULL || strcmp(allow_null, "false") == 0)
			arg->nullable = 0;
		else if (strcmp(allow_null, "true") == 0)
			arg->nullable = 1;
		else
			fail(&ctx->loc, "invalid value for allow-null attribute (%s)", allow_null);

		if (allow_null != NULL && !is_nullable_type(arg))
			fail(&ctx->loc, "allow-null is only valid for objects, strings, and arrays");

		arg->summary = NULL;
		if (summary)
			arg->summary = xstrdup(summary);

		wl_list_insert(ctx->message->arg_list.prev, &arg->link);
		ctx->message->arg_count++;
	} else if (strcmp(element_name, "enum") == 0) {
		if (name == NULL)
			fail(&ctx->loc, "no enum name given");

		enumeration = xmalloc(sizeof *enumeration);
		enumeration->name = xstrdup(name);
		enumeration->uppercase_name = uppercase_dup(name);
		enumeration->description = NULL;
		wl_list_init(&enumeration->entry_list);

		wl_list_insert(ctx->interface->enumeration_list.prev,
			       &enumeration->link);

		ctx->enumeration = enumeration;
	} else if (strcmp(element_name, "entry") == 0) {
		if (name == NULL)
			fail(&ctx->loc, "no entry name given");

		entry = xmalloc(sizeof *entry);
		entry->name = xstrdup(name);
		entry->uppercase_name = uppercase_dup(name);
		entry->value = xstrdup(value);
		if (summary)
			entry->summary = xstrdup(summary);
		else
			entry->summary = NULL;
		wl_list_insert(ctx->enumeration->entry_list.prev,
			       &entry->link);
	} else if (strcmp(element_name, "description") == 0) {
		if (summary == NULL)
			fail(&ctx->loc, "description without summary");

		description = xmalloc(sizeof *description);
		description->summary = xstrdup(summary);

		if (ctx->message)
			ctx->message->description = description;
		else if (ctx->enumeration)
			ctx->enumeration->description = description;
		else if (ctx->interface)
			ctx->interface->description = description;
		else
			ctx->protocol->description = description;
		ctx->description = description;
	}
	else
        {
		
                printf ("Description %s \n", element_name);
        }
}

static void
end_element(void *data, const XML_Char *name)
{
	struct parse_context *ctx = data;

	if (strcmp(name, "copyright") == 0) {
		ctx->protocol->copyright =
			strndup(ctx->character_data,
				ctx->character_data_length);
	} else if (strcmp(name, "description") == 0) {
		ctx->description->text =
			strndup(ctx->character_data,
				ctx->character_data_length);
	} else if (strcmp(name, "request") == 0 ||
		   strcmp(name, "event") == 0) {
		ctx->message = NULL;
	} else if (strcmp(name, "enum") == 0) {
		ctx->enumeration = NULL;
	}
}

static void
character_data(void *data, const XML_Char *s, int len)
{
	struct parse_context *ctx = data;

	if (ctx->character_data_length + len > sizeof (ctx->character_data)) {
		fprintf(stderr, "too much character data");
		exit(EXIT_FAILURE);
	    }

	memcpy(ctx->character_data + ctx->character_data_length, s, len);
	ctx->character_data_length += len;
}


static void
emit_type(struct arg *a)
{
	switch (a->type) {
	default:
	case INT:
	case FD:
		printf("int32_t");
		break;
	case NEW_ID:
	case UNSIGNED:
		printf("uint32_t");
		break;
	case FIXED:
		printf("wl_fixed_t");
		break;
	case STRING:
		printf("const char * ");
		break;
	case OBJECT:
		printf("object");
		break;
	case ARRAY:
		printf("struct wl_array * ");
		break;
	}
}

static void
emit_enumerations(struct wl_list *enum_list, struct interface *interface)
{
	struct enumeration *e;
	struct entry *entry;

	if (wl_list_empty(enum_list))
		return;
	printf("<br><br>");
	wl_list_for_each(e, &interface->enumeration_list, link) {
		struct description *desc = e->description;
		printf("<table width=\"100%%\" border=\"0\" cellpadding=\"5\" cellspacing=\"5\">");
		printf("<tr bgcolor=\"#E7F3F9\">");
                printf("<td><h3>Enumeration Name: %s</h3>", e->name);

		if (desc) {
			printf("\n");
			printf("<h3>Description<h3><blockquote> %s </blockquote>\n",
			        desc->summary);
                }
		if (wl_list_empty(&e->entry_list))
		{
		}
		else
		{
		        printf("\n<br><br>");
			printf("<blockquote>");
			printf("<table border=\"1\" cellpadding=\"5\" cellspacing=\"0\">");
			printf("<tr><td><b>Entry Name:</b></td>"
                               "<td><b> Entry Value:</b></td>"
                               "<td><b> Entry Description: </b></td>");
			printf("</tr>");
			wl_list_for_each(entry, &e->entry_list, link) {
			        printf("<tr>");
				printf("<td> %s</td> <td align=\"center\"> %s </td> <td> %s </td>\n",
					entry->name,
					entry->value,
					entry->summary );
			        printf("</tr>\n");
			}
			printf("</table> </blockquote>");
		}
		printf("</table>\n");
	}
}

static void
emit_request(struct wl_list *message_list, struct interface *interface)
{
	if (wl_list_empty(message_list))
		return;
	printf("<br><br>");
        struct message *e;
        struct arg     *arg;
        wl_list_for_each(e, &interface->request_list, link) {
                struct description *desc = e->description;
                printf("<table width=\"100%%\" border=\"0\" cellpadding=\"5\" cellspacing=\"5\">");
				printf("<tr bgcolor=\"#E7F3F9\">");
                printf("<td><h3>Request Name: %s</h3>",
                       e->name);
		if (e->since != 1)
                {
                	printf("<h3>Since</h3><blockquote> %d </blockquote>\n",
                       		e->since);
		}
                if (desc) {
			printf("\n");
                        printf("<h3>Request Summary</h3><blockquote> %s </blockquote>\n",
                                desc->summary);
                        printf("<h3>Request Details</h3><blockquote> %s </blockquote>\n",
                                desc->text);
                }

		if (wl_list_empty(&e->arg_list))
		{
		}
		else
                {
		   printf("\n<br><br>");
		   printf("<blockquote>");
		   printf("<table border=\"1\" cellpadding=\"5\" cellspacing=\"0\">");
		   printf("<tr><td><b> Argument Name </b></td>"
                               "<td><b> Argument Type </b></td>"
                               "<td><b> Nullable </b></td>"
                               "<td><b> Object Type </b></td>");
		   printf("</tr>");
                   wl_list_for_each(arg, &e->arg_list, link) 
                   {

			printf("<tr>");
                        printf("<td> %s </td> ",
                                arg->name);
                        printf("<td align=\"center\">");
                        emit_type(arg);
                        printf(" </td>");
                        printf("<td> ");
 			if (is_nullable_type(arg))
			{
				if (arg->nullable)
				{
                                	printf("TRUE");
				}
				else
				{
                                	printf("FALSE");
				}
			}
			else
			{
                               	printf(" Not Supported");
			}
                        printf(" </td>");
                        printf("<td> ");
                        if (arg->interface_name != NULL)
                        { 
                           printf("%s",
                                arg->interface_name);
                        }
                        printf("</td> ");
                        if (arg->summary != NULL)
			{
			     printf("</tr>\n");
			     printf("<tr><td>\n");
                             printf("Argument Summary: %s \n",
                                arg->summary);
			}
			printf("</td></tr>\n");
                      
                   }
		   printf("</table></blockquote></td></tr></table>\n");
			
              }
           printf("</blockquote></table>\n");   
        }


}

static void
emit_event(struct wl_list *message_list, struct interface *interface)
{
	if (wl_list_empty(message_list))
		return;
	printf("<br><br>");
        struct message *e;
        struct arg     *arg;

        wl_list_for_each(e, &interface->event_list, link) {
                struct description *desc = e->description;
				printf("<table width=\"100%%\" border=\"0\" cellpadding=\"5\" cellspacing=\"5\">");
				printf("<tr bgcolor=\"#E7F3F9\">");
                printf("<td><h3>Event Name: %s</h3>",
                       e->name);
		if (e->since != 1)
                {
                	printf("<h3>Since:</h3> <blockquote> %d </blockquote>",
                       		e->since);
		}
                if (desc) {
			printf("\n");
                        printf("<h3>Event Summary</h3> <blockquote> %s </blockquote>",
                                desc->summary);
                        printf("<h3>Event Details</h3> <blockquote> %s </blockquote>",
                                desc->text);
                }

		if (wl_list_empty(&e->arg_list))
		{
		}
		else
                {
		   printf("\n<br><br>");
		   printf("<blockquote>");
		   printf("<table border=\"1\" cellpadding=\"5\" cellspacing=\"0\">");
		   printf("<tr><td><b> Argument Name </b></td>"
                               "<td><b> Argument Type </b></td>"
                               "<td><b> Nullable </b></td>"
                               "<td><b> Object Type </b></td>");
		   printf("</tr>");
                   wl_list_for_each(arg, &e->arg_list, link) 
                   {

			printf("<tr>\n");
                        printf("<td> %s </td> ",
                                arg->name);
                        printf("<td align=\"center\">");
                        emit_type(arg);
                        printf(" </td>");
                        printf("<td> ");
 			if (is_nullable_type(arg))
			{
				if (arg->nullable)
				{
                                	printf("TRUE");
				}
				else
				{
                                	printf("FALSE");
				}
			}
			else
			{
                               	printf(" Not Supported");
			}
                        printf(" </td>");
                        printf("<td> ");
                        if (arg->interface_name != NULL)
                        { 
                           printf("%s",
                                arg->interface_name);
                        }
                        printf("</td> ");
                        if (arg->summary != NULL)
			{
			     printf("</tr>\n");
			     printf("<tr><td>\n");
                             printf("Argument Summary: %s \n",
                                arg->summary);
			}
			printf("</td></tr>\n");
                      
                   }
		    printf("</table></blockquote></td></tr></table>\n");
			
              }
         printf("</blockquote></table>\n");   
        }


}

static void
format_copyright(const char *copyright)
{
	int bol = 1, start = 0, i;

	for (i = 0; copyright[i]; i++) {
		if (bol && (copyright[i] == ' ' || copyright[i] == '\t')) {
			continue;
		} else if (bol) {
			bol = 0;
			start = i;
		}

		if (copyright[i] == '\n' || copyright[i] == '\0') {
			printf("%s %.*s\n",
			       i == 0 ? "/*" : " *",
			       i - start, copyright + start);
			bol = 1;
		}
	}
	printf(" */\n\n");
}

static void
emit_html(struct protocol *protocol)
{
	struct interface *i;

        printf("<HTML>"
               "<HEAD>"
               "<style>"
					"body, table, div, p, dl {"
					"	font: 400 13px/19px Lucida Grande, Verdana, Geneva, Arial,sans-serif;"
					"	line-height: 1.3;"
					"}"
					"h1 {"
					"	font-size: 150%%;"
					"	color: #3d578c;"
					"}"
					"h2 {"
					" 	border-bottom: 1px solid #879ECB;"
					" 	color: #354C7B;"
					" 	font-size: 120%%;"
					" 	font-weight: normal;"
					" 	margin-top: 1.75em;"
					" 	padding-top: 8px;"
					" 	padding-bottom: 4px;"
					" 	width: 100%%;"
					"}"
					"h3 {"
					"	font-size: 100%%;"
					"}"
					"a {"
					"	color: #3D578C;"
					"	font-weight: normal;"
					"	text-decoration: none;"
					"}"
					"td {"
					"	color:#3d578c;"
					"}"
					"blockquote {"
					"	background-color: #F7F8FB;"
					" 	border-left: 2px solid #9CAFD4;"
					"	margin: 0 24px 0 4px;"
					"	padding: 0 12px 0 16px;"
					"}"
				"</style>"	
               "<TITLE> Protocol Name: %s </TITLE>\n"
               "</head>\n", 
	       protocol->uppercase_name );

	printf("\n");
	printf("<body text=\"#000000\" bgcolor=\"#FFFFFF\" link=\"#0000FF\" alink=\"#FF0000\" vlink=\"#FF0000\">");
        printf("<h1>Protocol Name: %s </h1>",
               protocol->name);

	wl_list_for_each(i, &protocol->interface_list, link) {

               printf("\n\n<h2>Interface Name:%s Version %d</h2>\n\n\n", i->name, i->version );
               
               if (i->description == NULL)
               {
               		printf("<h3><a name=\"des\"><b>Description Summary</b>:</h3></a> <blockquote>NONE  </blockquote>\n"); 
               		printf("<h3><a name=\"des2\"><b>Description Details:</b></h3></a> <blockquote> NONE </blockquote> \n");
               }
	       else
	       {
			printf("<h3><a name=\"des\"><b>Description Summary</b>:</h3></a> <blockquote> %s </blockquote>\n",i->description->summary); 
               		printf("<h3><a name=\"des2\"><b>Description Details:</b></h3></a> <blockquote> %s </blockquote> \n",i->description->text);
               }
		emit_enumerations(&i->enumeration_list, i);
	           emit_request(&i->request_list, i);
               emit_event(&i->event_list, i);

	}

        printf("</BODY>\n"
               "</HTML>\n");
}

static void
emit_messages(struct wl_list *message_list,
	      struct interface *interface, const char *suffix)
{
	struct message *m;
	struct arg *a;

	if (wl_list_empty(message_list))
		return;

	printf("static const struct wl_message "
	       "%s_%s[] = {\n",
	       interface->name, suffix);

	wl_list_for_each(m, message_list, link) {
		printf("\t{ \"%s\", \"", m->name);

		if (m->since > 1)
			printf("%d", m->since);

		wl_list_for_each(a, &m->arg_list, link) {
			if (is_nullable_type(a) && a->nullable)
				printf("?");

			switch (a->type) {
			default:
			case INT:
				printf("i");
				break;
			case NEW_ID:
				if (a->interface_name == NULL)
					printf("su");
				printf("n");
				break;
			case UNSIGNED:
				printf("u");
				break;
			case FIXED:
				printf("f");
				break;
			case STRING:
				printf("s");
				break;
			case OBJECT:
				printf("o");
				break;
			case ARRAY:
				printf("a");
				break;
			case FD:
				printf("h");
				break;
			}
		}
		printf("\", types + %d },\n", m->type_index);
	}

	printf("};\n\n");
}

int main(int argc, char *argv[])
{
	struct parse_context ctx;
	struct protocol protocol;
	int bufsize;
	char *buf;
        FILE *fp ;

        if (argc != 2)
        {
		usage();
		return -1;
        }
	wl_list_init(&protocol.interface_list);
	protocol.type_index = 0;
	protocol.null_run_length = 0;
	protocol.copyright = NULL;
	memset(&ctx, 0, sizeof ctx);
	ctx.protocol = &protocol;

	ctx.loc.filename = argv[1];
	ctx.parser = XML_ParserCreate(NULL);
	XML_SetUserData(ctx.parser, &ctx);
	if (ctx.parser == NULL) {
		fprintf(stderr, "failed to create parser\n");
		exit(EXIT_FAILURE);
	}

	XML_SetElementHandler(ctx.parser, start_element, end_element);
	XML_SetCharacterDataHandler(ctx.parser, character_data);

        fp = fopen(argv[1], "r");
        if (fp == NULL)
        {
              fputs("Error opening file - file fopen ", stderr);
              return -1;
        }
        else
        {
           /* Go to the end of the file. */
           if (fseek(fp, 0L, SEEK_END) != 0) 
           {
              fputs("Error reading file - file fseek ", stderr);
              return -1;
           }

           /* Get the size of the file. */
           bufsize = ftell(fp);
           if (bufsize == -1) 
           { 
              fputs("Error reading file - file size ", stderr);
              return -1;
           }

           /* Allocate our buffer to that size. */
           buf = XML_GetBuffer(ctx.parser, (sizeof(char) * (bufsize + 1)) );

           /* Go back to the start of the file. */
           if (fseek(fp, 0L, SEEK_SET) != 0) 
           { 
               fputs("Error reading file", stderr); 
	       return -1 ;
           }

           /* Read the entire file into memory. */
           size_t newLen = fread(buf, sizeof(char), bufsize, fp);
           if (newLen == 0) 
           {
               fputs("Error reading file", stderr);
               return -1;
           } 
           fclose(fp);
        }
    
	XML_ParseBuffer(ctx.parser, bufsize, bufsize == 0);

	XML_ParserFree(ctx.parser);

 	emit_html(&protocol);

	return 0;
}
