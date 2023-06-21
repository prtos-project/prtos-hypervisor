/*
 * FILE: parser.h
 *
 * prtos's XML configuration parser to C
 *
 * www.prtos.org
 */

#ifndef _PARSER_H_
#define _PARSER_H_

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlschemas.h>

struct attr_xml {
    const xmlChar *name;
    void (*handler)(xmlNodePtr, const xmlChar *);
};

struct node_xml {
    const xmlChar *name;
    void (*handler_b_node)(xmlNodePtr);
    void (*handler_e_attr)(xmlNodePtr);
    void (*handler_e_node)(xmlNodePtr);
    struct attr_xml **attrList;
    struct node_xml **children;  //[];
};

extern struct node_xml *root_handlers[];

#endif
