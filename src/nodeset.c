/** 
 * XMLSec library
 *
 * Nodes set
 *
 * See Copyright for the status of this software.
 * 
 * Author: Aleksey Sanin <aleksey@aleksey.com>
 */
#include "globals.h"

#include <stdlib.h>
#include <string.h>
 
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <xmlsec/xmlsec.h>
#include <xmlsec/nodeset.h>
#include <xmlsec/errors.h>

#define xmlSecGetParent(node)           \
    (((node)->type != XML_NAMESPACE_DECL) ? \
        (node)->parent : \
        (xmlNodePtr)((xmlNsPtr)(node))->next)
	
static int	xmlSecNodeSetOneContains		(xmlSecNodeSetPtr nset, 
							 xmlNodePtr node, 
							 xmlNodePtr parent);
static int	xmlSecNodeSetWalkRecursive		(xmlSecNodeSetPtr nset, 
							 xmlSecNodeSetWalkCallback walkFunc, 
							 void* data, 
							 xmlNodePtr cur, 
							 xmlNodePtr parent);

xmlSecNodeSetPtr
xmlSecNodeSetCreate(xmlDocPtr doc, xmlNodeSetPtr nodes, xmlSecNodeSetType type) {
    xmlSecNodeSetPtr nset;

    nset = (xmlSecNodeSetPtr)xmlMalloc(sizeof(xmlSecNodeSet));
    if(nset == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "sizeof(xmlSecNodeSet)=%d",
		    sizeof(xmlSecNodeSet));
	return(NULL);
    }
    memset(nset, 0,  sizeof(xmlSecNodeSet));
    
    nset->doc 	= doc;
    nset->nodes = nodes;
    nset->type	= type;
    nset->next 	= nset->prev = nset;
    return(nset);
}

void
xmlSecNodeSetDestroy(xmlSecNodeSetPtr nset) {
    xmlSecNodeSetPtr tmp;

    xmlSecAssert(nset != NULL);
    	
    while((tmp = nset) != NULL) {
	if(nset->next != nset) {
	    nset->next->prev = nset->prev;
	    nset->prev->next = nset->next;	    
	    nset = nset->next;
	} else {
	    nset = NULL;
	}
	
	if(tmp->nodes != NULL) {
    	    xmlXPathFreeNodeSet(tmp->nodes);
	}
	if(tmp->children != NULL) {
	    xmlSecNodeSetDestroy(tmp->children);
	}
	memset(tmp, 0,  sizeof(xmlSecNodeSet));
        xmlFree(tmp);
    }
}

static int
xmlSecNodeSetOneContains(xmlSecNodeSetPtr nset, xmlNodePtr node, xmlNodePtr parent) {
    int in_nodes_set = 1;
    
    xmlSecAssert2(nset != NULL, 0);
    xmlSecAssert2(node != NULL, 0);
        
    /* special cases: */
    switch(nset->type) {
	case xmlSecNodeSetTreeWithoutComments:
        case xmlSecNodeSetTreeWithoutCommentsInvert:
	    if(node->type == XML_COMMENT_NODE) {
		return(0);
	    }
	    break;
	case xmlSecNodeSetList:
	    return(xmlSecNodeSetContains(nset->children, node, parent));
	default:
	    break;
    }
        
    if(nset->nodes != NULL) {
	if(node->type != XML_NAMESPACE_DECL) {
	    in_nodes_set = xmlXPathNodeSetContains(nset->nodes, node);
	} else {
	    xmlNs ns;
	    
	    memcpy(&ns, node, sizeof(ns)); 
	    ns.next = (xmlNsPtr)parent; /* this is a libxml hack! check xpath.c for details */

	    /* 
	     * If the input is an XPath node-set, then the node-set must explicitly 
	     * contain every node to be rendered to the canonical form.
	     */
	    in_nodes_set = (xmlXPathNodeSetContains(nset->nodes, (xmlNodePtr)&ns));
	}
    }
    
    switch(nset->type) {
    case xmlSecNodeSetNormal:
	return(in_nodes_set);
    case xmlSecNodeSetInvert:
	return(!in_nodes_set);
    case xmlSecNodeSetTree:
    case xmlSecNodeSetTreeWithoutComments:
	if(in_nodes_set) {
	    return(1);
	}
	if((parent != NULL) && (parent->type == XML_ELEMENT_NODE)) {
	    return(xmlSecNodeSetOneContains(nset, parent, parent->parent));
	}
	return(0);
    case xmlSecNodeSetTreeInvert:
    case xmlSecNodeSetTreeWithoutCommentsInvert:
	if(in_nodes_set) {
	    return(0);
	}
	if((parent != NULL) && (parent->type == XML_ELEMENT_NODE)) {
	    return(xmlSecNodeSetOneContains(nset, parent, parent->parent));
	}
	return(1);
    default:
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TYPE,
		    "nodes set type %d", nset->type);
    }
    
    return(0);
}

int
xmlSecNodeSetContains(xmlSecNodeSetPtr nset, xmlNodePtr node, xmlNodePtr parent) {
    int status = 1;
    xmlSecNodeSetPtr cur;
    
    xmlSecAssert2(node != NULL, 0);
    
    /* special cases: */
    if(nset == NULL) {
	return(1);
    }
    
    status = 1;
    cur = nset;
    do {
	switch(cur->op) {
	case xmlSecNodeSetIntersection:
	    if(status && !xmlSecNodeSetOneContains(cur, node, parent)) {
		status = 0;
	    }
    	    break;
	case xmlSecNodeSetSubtraction:
	    if(status && xmlSecNodeSetOneContains(cur, node, parent)) {
		status = 0;
	    }
	    break;
	case xmlSecNodeSetUnion:
	    if(!status && xmlSecNodeSetOneContains(cur, node, parent)) {
		status = 1;
	    }	
	    break;
	default:
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_INVALID_TYPE,
			"operation type %d", cur->op);
	    return(-1);
	}
	cur = cur->next;
    } while(cur != nset);
    
    return(status);
}

xmlSecNodeSetPtr	
xmlSecNodeSetAdd(xmlSecNodeSetPtr nset, xmlSecNodeSetPtr newNSet, xmlSecNodeSetOp op) {
    xmlSecAssert2(newNSet != NULL, NULL);
    xmlSecAssert2(newNSet->next == newNSet, NULL);

    newNSet->op	= op;
    if(nset == NULL) {
	return(newNSet);
    }
        
    newNSet->next = nset;
    newNSet->prev = nset->prev;
    nset->prev->next = newNSet;
    nset->prev 	     = newNSet;
    return(nset);
}

xmlSecNodeSetPtr	
xmlSecNodeSetAddList(xmlSecNodeSetPtr nset, xmlSecNodeSetPtr newNSet, xmlSecNodeSetOp op) {
    xmlSecNodeSetPtr tmp1, tmp2;

    xmlSecAssert2(newNSet != NULL, NULL);
    
    tmp1 = xmlSecNodeSetCreate(newNSet->doc, NULL, xmlSecNodeSetList);
    if(tmp1 == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecNodeSetCreate");
	return(NULL);
    }
    tmp1->children = newNSet;
    
    tmp2 = xmlSecNodeSetAdd(nset, tmp1, op);
    if(tmp2 == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecNodeSetAdd");
	xmlSecNodeSetDestroy(tmp1);
	return(NULL);
    }
    return(tmp2);
}

 
int
xmlSecNodeSetWalk(xmlSecNodeSetPtr nset, xmlSecNodeSetWalkCallback walkFunc, void* data) {
    xmlSecAssert2(nset != NULL, -1);
    xmlSecAssert2(walkFunc != NULL, -1);

    /* special cases */
    if(nset->nodes != NULL) {
        int ret = 0;
	int i;

	switch(nset->type) {
	case xmlSecNodeSetNormal:
	    for(i = 0; (ret >= 0) && (i < nset->nodes->nodeNr); ++i) {
		ret = walkFunc(nset, nset->nodes->nodeTab[i], 
			       xmlSecGetParent(nset->nodes->nodeTab[i]), 
			       data);
	    }
	    return(ret);
	case xmlSecNodeSetTree:
	case xmlSecNodeSetTreeWithoutComments:
	    for(i = 0; (ret >= 0) && (i < nset->nodes->nodeNr); ++i) {
		ret = xmlSecNodeSetWalkRecursive(nset, walkFunc, data, 
		    nset->nodes->nodeTab[i], 
		    xmlSecGetParent(nset->nodes->nodeTab[i]));
	    }
	    return(ret);
	default:
	    break;
	}
    }

    /* other cases */	
    if(nset->doc == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    "nset->doc is null");
	return(-1);
    }
    
    return(xmlSecNodeSetWalkRecursive(nset, walkFunc, data, (xmlNodePtr)nset->doc, NULL));
}

static int
xmlSecNodeSetWalkRecursive(xmlSecNodeSetPtr nset, xmlSecNodeSetWalkCallback walkFunc, 
			    void* data, xmlNodePtr cur, xmlNodePtr parent) {
    int ret;

    xmlSecAssert2(nset != NULL, -1);
    xmlSecAssert2(cur != NULL, -1);
    xmlSecAssert2(walkFunc != NULL, -1);
    
    /* the node itself */
    if(xmlSecNodeSetContains(nset, cur, parent)) {
	ret = walkFunc(nset, cur, parent, data);
	
	if(ret < 0) {
	    return(ret);
	}
    }
	
    /* element node has attributes, namespaces  */	
    if(cur->type == XML_ELEMENT_NODE) {
        xmlAttrPtr attr;
	xmlNodePtr node;
	xmlNsPtr ns, tmp;
	
        attr = (xmlAttrPtr)cur->properties;
        while(attr != NULL) {
	    if(xmlSecNodeSetContains(nset, (xmlNodePtr)attr, cur)) {
		ret = walkFunc(nset, (xmlNodePtr)attr, cur, data);
		if(ret < 0) {
		    return(ret);
		}
	    }
	    attr = attr->next;
	}

	node = cur;
	while(node != NULL) {
    	    ns = node->nsDef;
    	    while(ns != NULL) {
		tmp = xmlSearchNs(nset->doc, cur, ns->prefix);
		if((tmp == ns) && xmlSecNodeSetContains(nset, (xmlNodePtr)ns, cur)) {
		    ret = walkFunc(nset, (xmlNodePtr)ns, cur, data);
		    if(ret < 0) {
			return(ret);
		    }
		}
		ns = ns->next;
	    }
	    node = node->parent;
	}
    }

    /* element and document nodes have children */
    if((cur->type == XML_ELEMENT_NODE) || (cur->type == XML_DOCUMENT_NODE)) {
	xmlNodePtr node;
	
	node = cur->children;
	while(node != NULL) {
	    ret = xmlSecNodeSetWalkRecursive(nset, walkFunc, data, node, cur);
	    if(ret < 0) {
		return(ret);
	    }
	    node = node->next;
	}
    }
    return(0);
}

xmlSecNodeSetPtr	
xmlSecNodeSetGetChildren(xmlDocPtr doc, const xmlNodePtr parent, int withComments, int invert) {
    xmlNodeSetPtr nodes;
    xmlSecNodeSetType type;

    xmlSecAssert2(doc != NULL, NULL);
    xmlSecAssert2(parent != NULL, NULL);
        
    nodes = xmlXPathNodeSetCreate(parent);
    if(nodes == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XML_FAILED,
		    "xmlXPathNodeSetCreate");
	return(NULL);
    }	

    if(withComments && invert) {
	type = xmlSecNodeSetTreeInvert;
    } else if(withComments && !invert) {
	type = xmlSecNodeSetTree;
    } else if(!withComments && invert) {
	type = xmlSecNodeSetTreeWithoutCommentsInvert;
    } else { /* if(!withComments && !invert) */
	type = xmlSecNodeSetTreeWithoutComments;
    }

    return(xmlSecNodeSetCreate(doc, nodes, type));
}

void
xmlSecNodeSetDebugDump(xmlSecNodeSetPtr nset, FILE *output) {
    int i, l;
    xmlNodePtr cur;

    xmlSecAssert(nset != NULL);
    xmlSecAssert(output != NULL);

    fprintf(output, "== Nodes set ");
    switch(nset->type) {
    case xmlSecNodeSetNormal:
	fprintf(output, "(xmlSecNodeSetNormal)\n");
	break;
    case xmlSecNodeSetInvert:
	fprintf(output, "(xmlSecNodeSetInvert)\n");
	break;
    case xmlSecNodeSetTree:
	fprintf(output, "(xmlSecNodeSetTree)\n");
	break;
    case xmlSecNodeSetTreeWithoutComments:
	fprintf(output, "(xmlSecNodeSetTreeWithoutComments)\n");
	break;
    case xmlSecNodeSetTreeInvert:
	fprintf(output, "(xmlSecNodeSetTreeInvert)\n");
	break;
    case xmlSecNodeSetTreeWithoutCommentsInvert:
	fprintf(output, "(xmlSecNodeSetTreeWithoutCommentsInvert)\n");
	break;
    case xmlSecNodeSetList:
	fprintf(output, "(xmlSecNodeSetList)\n");
	fprintf(output, ">>>\n");
	xmlSecNodeSetDebugDump(nset->children, output);
	fprintf(output, "<<<\n");
	return;
    default:
	fprintf(output, "(unknown=%d)\n", nset->type);
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TYPE,
		    "nodes set type %d", nset->type);
    }
        
    l = xmlXPathNodeSetGetLength(nset->nodes);
    for(i = 0; i < l; ++i) {
	cur = xmlXPathNodeSetItem(nset->nodes, i);
	if(cur->type != XML_NAMESPACE_DECL) {
	    fprintf(output, "%d: %s\n", cur->type, 
		(cur->name) ? cur->name : BAD_CAST "null");
	} else {
	    xmlNsPtr ns = (xmlNsPtr)cur;
	    fprintf(output, "%d: %s=%s (%s:%s)\n", cur->type, 
		(ns->prefix) ? ns->prefix : BAD_CAST "null",
		(ns->href) ? ns->href : BAD_CAST "null",
		(((xmlNodePtr)ns->next)->ns && 
		 ((xmlNodePtr)ns->next)->ns->prefix) ? 
		  ((xmlNodePtr)ns->next)->ns->prefix : BAD_CAST "null",		
		((xmlNodePtr)ns->next)->name);
	}
    }
}
