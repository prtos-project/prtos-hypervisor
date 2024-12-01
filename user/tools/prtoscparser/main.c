/*
 * FILE: main.c
 *
 * prtoscparser written with XML2 libs
 *
 * www.prtos.org
 */

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include "common.h"
#include "parser.h"
#include "prtos_conf.h"

#define TOOL_NAME "prtoscparser"
#define USAGE "usage: " TOOL_NAME " [-c] [-d] [-s <xsd_file>] [-o output_file] <PRTOS_CF.xml>\n"

char *in_file;

void line_error(int line_number, char *fmt, ...) {
    va_list args;

    fflush(stdout);
    fprintf(stderr, "%s:%d: ", in_file, line_number);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
    exit(2);
}

void error_printf(char *fmt, ...) {
    va_list args;

    fflush(stdout);
    if (TOOL_NAME != NULL) fprintf(stderr, "%s: ", TOOL_NAME);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] == ':') fprintf(stderr, " %s", strerror(errno));
    fprintf(stderr, "\n");
    exit(2); /* conventional value for failed execution */
}

extern const char start_xsd[];

static void process_xml_tree(xmlNodePtr root, struct node_xml *handlers[]) {
    xmlNodePtr node;
    int e, attr;

    for (node = root; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE) {
            for (e = 0; handlers[e]; e++) {
                if (!xmlStrcasecmp(node->name, handlers[e]->name)) {
                    if (handlers[e]->handler_b_node) (handlers[e]->handler_b_node)(node);
                    for (attr = 0; handlers[e]->attrList && handlers[e]->attrList[attr]; attr++) {
                        if (xmlHasProp(node, handlers[e]->attrList[attr]->name)) {
                            if (handlers[e]->attrList[attr]->handler)
                                (handlers[e]->attrList[attr]->handler)(node, xmlGetProp(node, handlers[e]->attrList[attr]->name));
                        }
                    }
                    if (handlers[e]->handler_e_attr) (handlers[e]->handler_e_attr)(node);
                    process_xml_tree(node->children, handlers[e]->children);

                    if (handlers[e]->handler_e_node) (handlers[e]->handler_e_node)(node);
                }
            }
        }
    }
}

static void parse_xml_file(const char *xml_file, const char *xsd_file) {
    xmlSchemaValidCtxtPtr valid_schema;
    xmlSchemaParserCtxtPtr xsd_parser;
    xmlSchemaPtr schema;
    xmlNodePtr cur;
    xmlDocPtr doc;

    if (xsd_file) {
        fprintf(stderr, "XSD overrided by \"%s\"\n", xsd_file);
        if (!(xsd_parser = xmlSchemaNewParserCtxt(xsd_file))) error_printf("Invalid XSD definition");
    } else {
        if (!(xsd_parser = xmlSchemaNewMemParserCtxt(start_xsd, strlen(start_xsd)))) error_printf("Invalid XSD definition");
    }
    if (!(schema = xmlSchemaParse(xsd_parser))) error_printf("Invalid XSD definition");
    valid_schema = xmlSchemaNewValidCtxt(schema);

    if (!(doc = xmlParseFile(xml_file))) error_printf("XML file \"%s\" not found", xml_file);

    xmlSchemaSetValidOptions(valid_schema, XML_SCHEMA_VAL_VC_I_CREATE);
    if (xmlSchemaValidateDoc(valid_schema, doc)) error_printf("XML file \"%s\" invalid", xml_file);

    cur = xmlDocGetRootElement(doc);
    process_xml_tree(cur, root_handlers);
    xmlSchemaFreeValidCtxt(valid_schema);
    xmlSchemaFreeParserCtxt(xsd_parser);
    xmlSchemaFree(schema);

    xmlFreeDoc(doc);
    xmlCleanupParser();
}

#define DEFAULT_OUTFILE_C "a.c.prtos_conf"
#define DEFAULT_OUTFILE_BIN "a.bin.prtos_conf"

int main(int argc, char **argv) {
    char *xsd_file = NULL, *out_file_name = 0, *c_file_name = 0, *path, *app;
    FILE *out_file = 0;
    int opt, only_c_code = 0;

    /* To check that the libxml version in use is compatible with the version the software has been compiled against */
    LIBXML_TEST_VERSION;

    while ((opt = getopt(argc, argv, "do:s:c")) != -1) {
        switch (opt) {
            case 'c':
                only_c_code = 1;
                break;
            case 'd':
                fprintf(stderr, "%s\n", start_xsd);
                exit(0);
                break;
            case 'o':
                out_file_name = strdup(optarg);
                break;
            case 's':
                DO_MALLOC(xsd_file, strlen(optarg) + 1);
                strcpy(xsd_file, optarg);
                break;
            default: /* ? */
                fprintf(stderr, USAGE);
                exit(0);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, USAGE);
        exit(0);
    }

    DO_MALLOC(in_file, strlen(argv[optind]) + 1);
    strcpy(in_file, argv[optind]);
    parse_xml_file(in_file, xsd_file);

    if (only_c_code) {
        if (!out_file_name) out_file_name = strdup(DEFAULT_OUTFILE_C);
        c_file_name = out_file_name;
    } else {
        if (!out_file_name) out_file_name = strdup(DEFAULT_OUTFILE_BIN);
        c_file_name = strdup(DEFAULT_OUTFILE_C);
    }

    if (!(out_file = fopen(c_file_name, "w"))) {
        fprintf(stderr, "fopen: unable open/create file %s\n", c_file_name);
        exit(-1);
    }

    generate_c_file(out_file);

    fclose(out_file);

    if (only_c_code) return 0;

    path = dirname(strdup(argv[0]));
    app = basename(strdup(argv[0]));

    if (!strcmp(argv[0], app)) path = 0;

    exec_xml_conf_build(path, c_file_name, out_file_name);
    calc_digest(out_file_name);

    unlink(c_file_name);
    return 0;
}
