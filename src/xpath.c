/** 
 * XMLSec library
 *
 * XPath transform
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
#include <libxml/xpointer.h>

#include <xmlsec/xmlsec.h>
#include <xmlsec/xmltree.h>
#include <xmlsec/keys.h>
#include <xmlsec/transforms.h>
#include <xmlsec/transformsInternal.h>
#include <xmlsec/xpath.h>
#include <xmlsec/debug.h>
#include <xmlsec/errors.h>


typedef enum {
    xmlSecXPathTypeXPath,
    xmlSecXPathTypeXPath2,
    xmlSecXPathTypeXPointer
#ifdef XMLSEC_XPATH2_ALLOW_XPOINTER
    , xmlSecXPathTypeXPointer2
#endif /* XMLSEC_XPATH2_ALLOW_XPOINTER */    
} xmlSecXPathType;

/* XPath transform */
typedef struct _xmlSecXPathData xmlSecXPathData, *xmlSecXPathDataPtr;
struct _xmlSecXPathData {
    xmlChar			*expr;
    xmlChar			**nsList;
    size_t			nsListSize;
    xmlSecXPathType		xpathType;
    xmlSecXPath2TransformType	xpath2Type;
    xmlSecXPathDataPtr		next;
};


static xmlSecXPathDataPtr xmlSecXPathDataCreate		(const xmlNodePtr node,
							 xmlSecXPathDataPtr prev,
							 xmlSecXPathType xpathType);
static void		  xmlSecXPathDataDestroy	(xmlSecXPathDataPtr data);
static int		  xmlSecXPathDataReadNode	(xmlSecXPathDataPtr data,
							 const xmlNodePtr node);
static int		  xmlSecXPathDataReadNsList	(xmlSecXPathDataPtr data,
							 const xmlNodePtr node);
static xmlSecNodeSetPtr	  xmlSecXPathDataExecute	(xmlSecXPathDataPtr data,
							 xmlDocPtr doc,
							 xmlNodePtr hereNode);

static xmlSecTransformPtr xmlSecTransformXPathCreate	(xmlSecTransformId id);
static void		xmlSecTransformXPathDestroy	(xmlSecTransformPtr transform);

static int 		xmlSecTransformXPathReadNode	(xmlSecTransformPtr transform,
							 xmlNodePtr transformNode);
static int 		xmlSecTransformXPathExecute	(xmlSecXmlTransformPtr transform,
							 xmlDocPtr ctxDoc,
							 xmlDocPtr *doc,
							 xmlSecNodeSetPtr *nodes);

static int 		xmlSecTransformXPath2ReadNode	(xmlSecTransformPtr transform,
							 xmlNodePtr transformNode);
static int 		xmlSecTransformXPath2Execute	(xmlSecXmlTransformPtr transform,
							 xmlDocPtr ctxDoc,
							 xmlDocPtr *doc,
							 xmlSecNodeSetPtr *nodes);

static int 		xmlSecTransformXPointerReadNode	(xmlSecTransformPtr transform,
							 xmlNodePtr transformNode);
static int 		xmlSecTransformXPointerExecute	(xmlSecXmlTransformPtr transform,
							 xmlDocPtr ctxDoc,
							 xmlDocPtr *doc,
							 xmlSecNodeSetPtr *nodes);

struct _xmlSecXmlTransformIdStruct xmlSecTransformXPathId = {
    /* same as xmlSecTransformId */ 
    xmlSecTransformTypeXml,		/* xmlSecTransformType type; */
    xmlSecUsageDSigTransform,		/* xmlSecTransformUsage	usage; */
    xmlSecXPathNs, /* const xmlChar *href; */

    xmlSecTransformXPathCreate,		/* xmlSecTransformCreateMethod create; */
    xmlSecTransformXPathDestroy,	/* xmlSecTransformDestroyMethod destroy; */
    xmlSecTransformXPathReadNode,	/* xmlSecTransformReadNodeMethod read; */
    
    /* xmlTransform info */
    xmlSecTransformXPathExecute		/* xmlSecXmlTransformExecuteMethod executeXml; */
};
xmlSecTransformId xmlSecTransformXPath = (xmlSecTransformId)(&xmlSecTransformXPathId);

struct _xmlSecXmlTransformIdStruct xmlSecTransformXPath2Id = {
    /* same as xmlSecTransformId */ 
    xmlSecTransformTypeXml,		/* xmlSecTransformType type; */
    xmlSecUsageDSigTransform,		/* xmlSecTransformUsage	usage; */
    xmlSecXPath2Ns, /* const xmlChar *href; */

    xmlSecTransformXPathCreate,		/* xmlSecTransformCreateMethod create; */
    xmlSecTransformXPathDestroy,	/* xmlSecTransformDestroyMethod destroy; */
    xmlSecTransformXPath2ReadNode,	/* xmlSecTransformReadNodeMethod read; */
    
    /* xmlTransform info */
    xmlSecTransformXPath2Execute	/* xmlSecXmlTransformExecuteMethod executeXml; */
};
xmlSecTransformId xmlSecTransformXPath2 = (xmlSecTransformId)(&xmlSecTransformXPath2Id);

struct _xmlSecXmlTransformIdStruct xmlSecTransformXPointerId = {
    /* same as xmlSecTransformId */ 
    xmlSecTransformTypeXml,		/* xmlSecTransformType type; */
    xmlSecUsageDSigTransform,		/* xmlSecTransformUsage	usage; */
    xmlSecXPointerNs, /* const xmlChar *href; */

    xmlSecTransformXPathCreate,		/* xmlSecTransformCreateMethod create; */
    xmlSecTransformXPathDestroy,	/* xmlSecTransformDestroyMethod destroy; */
    xmlSecTransformXPointerReadNode,	/* xmlSecTransformReadNodeMethod read; */
    
    /* xmlTransform info */
    xmlSecTransformXPointerExecute		/* xmlSecXmlTransformExecuteMethod executeXml; */
};
xmlSecTransformId xmlSecTransformXPointer = (xmlSecTransformId)(&xmlSecTransformXPointerId);


static const xmlChar xpathPattern[] = "(//. | //@* | //namespace::*)[%s]";

/** 
 * xmlSecXPathHereFunction:
 * @ctxt:
 * @nargs:
 *
 * see xmlXPtrHereFunction() in xpointer.c. the only change is that 
 * we return NodeSet instead of NodeInterval
 */
void 
xmlSecXPathHereFunction(xmlXPathParserContextPtr ctxt, int nargs) {
    CHECK_ARITY(0);

    if (ctxt->context->here == NULL)
	XP_ERROR(XPTR_SYNTAX_ERROR);
    
    valuePush(ctxt, xmlXPathNewNodeSet(ctxt->context->here));
}



/***************************************************************************
 *
 *         Common XPath/XPointer transforms functions
 *
 **************************************************************************/
/**
 * xmlSecTransformXPathCreate
 * @id:
 *
 *
 */
static xmlSecTransformPtr 
xmlSecTransformXPathCreate(xmlSecTransformId id) {
    xmlSecXmlTransformPtr xmlTransform; 

    xmlSecAssert2(id != NULL, NULL);
        
    if((id != xmlSecTransformXPath) && 
       (id != xmlSecTransformXPath2) && 
       (id != xmlSecTransformXPointer)) {
       
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecTransformXPath or xmlSecTransformXPath2 or xmlSecTransformXPointer");
	return(NULL);
    }
    
    xmlTransform = (xmlSecXmlTransformPtr)xmlMalloc(sizeof(struct _xmlSecXmlTransform));
    if(xmlTransform == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "sizeof(struct _xmlSecXmlTransform)=%d",
		    sizeof(struct _xmlSecXmlTransform));
	return(NULL);
    }
    memset(xmlTransform, 0,  sizeof(struct _xmlSecXmlTransform));
    xmlTransform->id = (xmlSecXmlTransformId)id;    
    return((xmlSecTransformPtr)xmlTransform);
}

/**
 * xmlSecTransformXPathDestroy
 * @transform:
 *
 *
 */
static void
xmlSecTransformXPathDestroy(xmlSecTransformPtr transform) {
    xmlSecXmlTransformPtr xmlTransform;
    xmlSecXPathDataPtr data;
    
    xmlSecAssert(transform != NULL);
    
    if(!xmlSecTransformCheckId(transform, xmlSecTransformXPath) && 
       !xmlSecTransformCheckId(transform, xmlSecTransformXPath2) &&
       !xmlSecTransformCheckId(transform, xmlSecTransformXPointer)) {

	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecTransformXPath or xmlSecTransformXPath2 or xmlSecTransformXPointer");
	return;
    }    
    xmlTransform = (xmlSecXmlTransformPtr)transform;
    data = (xmlSecXPathDataPtr)xmlTransform->xmlData;
    
    if(data != NULL) {
	xmlSecXPathDataDestroy(data);
    }
    memset(xmlTransform, 0,  sizeof(struct _xmlSecXmlTransform));  
    xmlFree(xmlTransform);
}

/***************************************************************************
 *
 *         XPath transform 
 *
 **************************************************************************/
/**
 * xmlSecTransformXPathAdd:
 * @transformNode: the transform node
 * @expression: the XPath expression
 * @namespaces: NULL terminated list of namespace prefix/href pairs
 *
 */
int 	
xmlSecTransformXPathAdd(xmlNodePtr transformNode, const xmlChar *expression,
			 const xmlChar **namespaces) {
    xmlNodePtr xpathNode;
    
    xmlSecAssert2(transformNode != NULL, -1);
    xmlSecAssert2(expression != NULL, -1);
    

    xpathNode = xmlSecFindChild(transformNode, BAD_CAST "XPath", xmlSecDSigNs);
    if(xpathNode != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_ALREADY_PRESENT,
		    "XPath");
	return(-1);    
    }

    xpathNode = xmlSecAddChild(transformNode, BAD_CAST "XPath", xmlSecDSigNs);
    if(xpathNode == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(XPath)");
	return(-1);    
    }
    
    
    xmlNodeSetContent(xpathNode, expression);
    if(namespaces != NULL) {	
	xmlNsPtr ns;
	const xmlChar *prefix;
    	const xmlChar *href;
	const xmlChar **ptr;
	
	ptr = namespaces;
	while((*ptr) != NULL) {
	    if(xmlStrEqual(BAD_CAST "#default", (*ptr))) {
		prefix = NULL;
	    } else {
		prefix = (*ptr);
	    }
	    if((++ptr) == NULL) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_INVALID_DATA,
			    "unexpected end of namespaces list");
		return(-1);
	    }
	    href = *(ptr++);

	    ns = xmlNewNs(xpathNode, href, prefix);
	    if(ns == NULL) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_XML_FAILED,
			    "xmlNewNs(%s, %s)", 
			    (href != NULL) ? (char*)href : "NULL", 
			    (prefix != NULL) ? (char*)prefix : "NULL");
		return(-1);
	    }
	}
    }
    return(0);
}


/**
 * xmlSecTransformXPathReadNode
 * @transform:
 * @transformNode:
 *
 * http://www.w3.org/TR/xmldsig-core/#sec-XPath
 */
static int 
xmlSecTransformXPathReadNode(xmlSecTransformPtr transform, xmlNodePtr transformNode) {
    xmlSecXmlTransformPtr xmlTransform;
    xmlSecXPathDataPtr data;
    xmlNodePtr cur;

    xmlSecAssert2(transform != NULL, -1);
    xmlSecAssert2(transformNode != NULL, -1);
    
    if(!xmlSecTransformCheckId(transform, xmlSecTransformXPath)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecTransformXPath");
	return(-1);
    }    
    xmlTransform = (xmlSecXmlTransformPtr)transform;
    
    /* There is only one required node XPath*/
    cur = xmlSecGetNextElementNode(transformNode->children);  
    if((cur == NULL) || (!xmlSecCheckNodeName(cur, BAD_CAST "XPath", xmlSecDSigNs))) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    "XPath");
	return(-1);
    }

    data = xmlSecXPathDataCreate(cur, NULL, xmlSecXPathTypeXPath);
    if(data == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecXPathDataCreate");
	return(-1);
    }

    cur = xmlSecGetNextElementNode(cur->next);        
    if(cur != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    (cur->name != NULL) ? (char*)cur->name : "NULL");
	xmlSecXPathDataDestroy(data);
	return(-1);
    }

    if(xmlTransform->xmlData != NULL) {
	xmlSecXPathDataDestroy((xmlSecXPathDataPtr)xmlTransform->xmlData);
    }
    xmlTransform->xmlData = data;
    xmlTransform->here 	  = transformNode;
    return(0);
}

/**
 * xmlSecTransformXPathExecute
 * @transform:
 * @ctxDoc:
 * @doc:
 * @nodes:
 *
 */
static int
xmlSecTransformXPathExecute(xmlSecXmlTransformPtr transform, xmlDocPtr ctxDoc,
			     xmlDocPtr *doc, xmlSecNodeSetPtr *nodes) {
    xmlSecXmlTransformPtr xmlTransform;
    xmlSecXPathDataPtr data;
    xmlNodePtr hereNode;
    xmlSecNodeSetPtr res;

    xmlSecAssert2(transform != NULL, -1);
    xmlSecAssert2(doc != NULL, -1);
    xmlSecAssert2((*doc) != NULL, -1);
    xmlSecAssert2(nodes != NULL, -1);

    if(!xmlSecTransformCheckId(transform, xmlSecTransformXPath)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecTransformXPath");
	return(-1);
    }    
    xmlTransform = (xmlSecXmlTransformPtr)transform;
    data = (xmlSecXPathDataPtr)xmlTransform->xmlData;

    xmlSecAssert2(data != NULL, -1);
    xmlSecAssert2(data->expr != NULL, -1);
    xmlSecAssert2(data->next == NULL, -1);
    
    /* function here() works only in he same document */  
    hereNode = ((*doc) == ctxDoc) ? xmlTransform->here : NULL;
    res = xmlSecXPathDataExecute(data, (*doc), hereNode);
    if(res == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecXPathDataExecute");
	return(-1);
    }

    (*nodes) = xmlSecNodeSetAdd((*nodes), res, xmlSecNodeSetIntersection);
    if((*nodes) == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecNodeSetAdd");
	xmlSecNodeSetDestroy(res);
	return(-1);
    }

    return(0);
}



/***************************************************************************
 *
 *         XPath2 transform 
 *
 **************************************************************************/
int
xmlSecTransformXPath2Add(xmlNodePtr transformNode, xmlSecXPath2TransformType type,
			const xmlChar *expression, const xmlChar **namespaces) {
    xmlNodePtr xpathNode;

    xmlSecAssert2(transformNode != NULL, -1);
    xmlSecAssert2(expression != NULL, -1);

    xpathNode = xmlSecAddChild(transformNode, BAD_CAST "XPath", xmlSecXPath2Ns);
    if(xpathNode == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(XPath)");
	return(-1);    
    }
    
    switch(type) {
    case xmlSecXPathTransformIntersect:
	xmlSetProp(xpathNode, BAD_CAST "Filter", BAD_CAST "intersect");
	break;
    case xmlSecXPathTransformSubtract:
	xmlSetProp(xpathNode, BAD_CAST "Filter", BAD_CAST "subtract");
	break;
    case xmlSecXPathTransformUnion:
	xmlSetProp(xpathNode, BAD_CAST "Filter", BAD_CAST "union");
	break;
    default:
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TYPE,
		    "type=%d", type);
	return(-1);    	
    }
    
    xmlNodeSetContent(xpathNode, expression);
    if(namespaces != NULL) {	
	xmlNsPtr ns;
	const xmlChar *prefix;
    	const xmlChar *href;
	const xmlChar **ptr;
	
	ptr = namespaces;
	while((*ptr) != NULL) {
	    if(xmlStrEqual(BAD_CAST "#default", (*ptr))) {
		prefix = NULL;
	    } else {
		prefix = (*ptr);
	    }
	    if((++ptr) == NULL) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_INVALID_DATA,
			    "unexpected end of namespaces list");
		return(-1);
	    }
	    href = *(ptr++);

	    ns = xmlNewNs(xpathNode, href, prefix);
	    if(ns == NULL) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_XML_FAILED,
			    "xmlNewNs(%s, %s)", 
			    (href != NULL) ? (char*)href : "NULL", 
			    (prefix != NULL) ? (char*)prefix : "NULL");
		return(-1);
	    }
	}
    }
    return(0);
}

/**
 * xmlSecTransformXPath2ReadNode
 * @transform:
 * @transformNode:
 *
 * http://www.w3.org/TR/xmldsig-core/#sec-XPath
 */
static int 
xmlSecTransformXPath2ReadNode(xmlSecTransformPtr transform, xmlNodePtr transformNode) {
    xmlSecXmlTransformPtr xmlTransform;
    xmlSecXPathDataPtr data = NULL;
    xmlSecXPathType xpathType = xmlSecXPathTypeXPath2;
    xmlNodePtr cur;

    xmlSecAssert2(transform != NULL, -1);
    xmlSecAssert2(transformNode != NULL, -1);
    
    if(!xmlSecTransformCheckId(transform, xmlSecTransformXPath2)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecTransformXPath2");
	return(-1);
    }    
    xmlTransform = (xmlSecXmlTransformPtr)transform;
    if(xmlTransform->xmlData != NULL) {
	xmlSecXPathDataDestroy((xmlSecXPathDataPtr)xmlTransform->xmlData);
	xmlTransform->xmlData = NULL;
    }

    /* There are only XPath nodes */
    cur = xmlSecGetNextElementNode(transformNode->children);  
    while(cur != NULL) {
	if(xmlSecCheckNodeName(cur, BAD_CAST "XPath", xmlSecXPath2Ns)) {
	    xpathType = xmlSecXPathTypeXPath2;
#ifdef XMLSEC_XPATH2_ALLOW_XPOINTER
	} else if(xmlSecCheckNodeName(cur, BAD_CAST "XPointer", xmlSecXPath2Ns)) {
	    xpathType = xmlSecXPathTypeXPointer2;
#endif /* XMLSEC_XPATH2_ALLOW_XPOINTER */
	} else {
	    break;
	}
	
        data = xmlSecXPathDataCreate(cur, data, xpathType);
	if(data == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecXPathDataCreate");
	    return(-1);
	}
	if(xmlTransform->xmlData == NULL) {
	    xmlTransform->xmlData = data;
	}
        cur = xmlSecGetNextElementNode(cur->next);  
    }

    if(cur != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    (cur->name != NULL) ? (char*)cur->name : "NULL");
	xmlSecXPathDataDestroy(data);
	return(-1);
    }
    xmlTransform->here 	  = transformNode;
    return(0);
}

/**
 * xmlSecTransformXPath2Execute
 * @transform:
 * @ctxDoc:
 * @doc:
 * @nodes:
 *
 */
static int
xmlSecTransformXPath2Execute(xmlSecXmlTransformPtr transform, xmlDocPtr ctxDoc,
			     xmlDocPtr *doc, xmlSecNodeSetPtr *nodes) {
    xmlSecXmlTransformPtr xmlTransform;
    xmlSecXPathDataPtr data;
    xmlNodePtr hereNode;
    xmlSecNodeSetPtr res = NULL;

    xmlSecAssert2(transform != NULL, -1);
    xmlSecAssert2(doc != NULL, -1);
    xmlSecAssert2((*doc) != NULL, -1);
    xmlSecAssert2(nodes != NULL, -1);
    
    if(!xmlSecTransformCheckId(transform, xmlSecTransformXPath2)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecTransformXPath2");
	return(-1);
    }    
    xmlTransform = (xmlSecXmlTransformPtr)transform;
    data = (xmlSecXPathDataPtr)xmlTransform->xmlData;
    hereNode = ((*doc) == ctxDoc) ? xmlTransform->here : NULL;

    xmlSecAssert2(data != NULL, -1);

    res = xmlSecXPathDataExecute(data, (*doc), hereNode);
    if(res == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecXPathDataExecute");
	return(-1);
    }

    (*nodes) = xmlSecNodeSetAddList((*nodes), res, xmlSecNodeSetIntersection);
    if((*nodes) == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecNodeSetAddList");
	xmlSecNodeSetDestroy(res);
	return(-1);
    }
    
    return(0);
}



/***************************************************************************
 *
 *         XPointer transform 
 *
 **************************************************************************/
/**
 * xmlSecTransformXPointerAdd:
 * @transformNode: the transform node
 * @expression: the XPointer expression
 * @namespaces: NULL terminated list of namespace prefix/href pairs
 *
 */
int 	
xmlSecTransformXPointerAdd(xmlNodePtr transformNode, const xmlChar *expression,
			 const xmlChar **namespaces) {
    xmlNodePtr xpointerNode;

    xmlSecAssert2(expression != NULL, -1);
    xmlSecAssert2(transformNode != NULL, -1);

    xpointerNode = xmlSecFindChild(transformNode, BAD_CAST "XPointer", xmlSecXPointerNs);
    if(xpointerNode != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_NODE_ALREADY_PRESENT,
		    "XPointer");
	return(-1);    
    }

    xpointerNode = xmlSecAddChild(transformNode, BAD_CAST "XPointer", xmlSecXPointerNs);
    if(xpointerNode == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecAddChild(XPath)");
	return(-1);    
    }
    
    
    xmlNodeSetContent(xpointerNode, expression);
    if(namespaces != NULL) {	
	xmlNsPtr ns;
	const xmlChar *prefix;
    	const xmlChar *href;
	const xmlChar **ptr;
	
	ptr = namespaces;
	while((*ptr) != NULL) {
	    if(xmlStrEqual(BAD_CAST "#default", (*ptr))) {
		prefix = NULL;
	    } else {
		prefix = (*ptr);
	    }
	    if((++ptr) == NULL) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_INVALID_DATA,
			    "unexpected end of namespaces list");
		return(-1);
	    }
	    href = *(ptr++);

	    ns = xmlNewNs(xpointerNode, href, prefix);
	    if(ns == NULL) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_XML_FAILED,
			    "xmlNewNs(%s, %s)", 
			    (href != NULL) ? (char*)href : "NULL", 
			    (prefix != NULL) ? (char*)prefix : "NULL");
		return(-1);
	    }
	}
    }
    return(0);
}


/**
 * xmlSecTransformXPointerReadNode
 * @transform:
 * @transformNode:
 *
 * http://www.ietf.org/internet-drafts/draft-eastlake-xmldsig-uri-02.txt
 */
static int 
xmlSecTransformXPointerReadNode(xmlSecTransformPtr transform, xmlNodePtr transformNode) {
    xmlSecXmlTransformPtr xmlTransform;
    xmlSecXPathDataPtr data;
    xmlNodePtr cur;

    xmlSecAssert2(transform != NULL, -1);
    xmlSecAssert2(transformNode != NULL, -1);
    
    if(!xmlSecTransformCheckId(transform, xmlSecTransformXPointer)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecTransformXPointer");
	return(-1);
    }    
    xmlTransform = (xmlSecXmlTransformPtr)transform;
    
    /* There is only one required node XPointer*/
    cur = xmlSecGetNextElementNode(transformNode->children);  
    if((cur == NULL) || (!xmlSecCheckNodeName(cur, BAD_CAST "XPointer", xmlSecXPointerNs))) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    "XPointer");
	return(-1);
    }

    data = xmlSecXPathDataCreate(cur, NULL, xmlSecXPathTypeXPointer);
    if(data == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecXPathDataCreate");
	return(-1);
    }

    cur = xmlSecGetNextElementNode(cur->next);        
    if(cur != NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE,
		    (cur->name != NULL) ? (char*)cur->name : "NULL");
	xmlSecXPathDataDestroy(data);
	return(-1);
    }

    if(xmlTransform->xmlData != NULL) {
	xmlSecXPathDataDestroy((xmlSecXPathDataPtr)xmlTransform->xmlData);
    }
    xmlTransform->xmlData = data;
    xmlTransform->here 	  = transformNode;
    return(0);
}

/**
 * xmlSecTransformXPointerExecute
 * @transform:
 * @ctxDoc:
 * @doc:
 * @nodes:
 *
 */
static int
xmlSecTransformXPointerExecute(xmlSecXmlTransformPtr transform, xmlDocPtr ctxDoc,
			     xmlDocPtr *doc, xmlSecNodeSetPtr *nodes) {
    xmlSecXmlTransformPtr xmlTransform;
    xmlSecXPathDataPtr data;
    xmlNodePtr hereNode;
    xmlSecNodeSetPtr res;

    xmlSecAssert2(transform != NULL, -1);
    xmlSecAssert2(doc != NULL, -1);
    xmlSecAssert2((*doc) != NULL, -1);
    xmlSecAssert2(nodes != NULL, -1);

    if(!xmlSecTransformCheckId(transform, xmlSecTransformXPointer)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecTransformXPointer");
	return(-1);
    }    
    xmlTransform = (xmlSecXmlTransformPtr)transform;
    data = (xmlSecXPathDataPtr)xmlTransform->xmlData;

    xmlSecAssert2(data != NULL, -1);
    xmlSecAssert2(data->expr != NULL, -1);
    xmlSecAssert2(data->next == NULL, -1);
    
    /* function here() works only in he same document */  
    hereNode = ((*doc) == ctxDoc) ? xmlTransform->here : NULL;
    res = xmlSecXPathDataExecute(data, (*doc), hereNode);
    if(res == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecXPathDataExecute");
	return(-1);
    }

    (*nodes) = xmlSecNodeSetAdd((*nodes), res, xmlSecNodeSetIntersection);
    if((*nodes) == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecNodeSetAdd");
	xmlSecNodeSetDestroy(res);
	return(-1);
    }

    return(0);
}



/***************************************************************************
 *
 *   XPath Transform Data
 *
 ***************************************************************************/ 
/**
 * xmlSecXPathDataCreate:
 *
 */
xmlSecXPathDataPtr	
xmlSecXPathDataCreate(const xmlNodePtr node, xmlSecXPathDataPtr prev, xmlSecXPathType xpathType) {
    xmlSecXPathDataPtr data;
    
    data = (xmlSecXPathDataPtr) xmlMalloc(sizeof(xmlSecXPathData));
    if(data == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "sizeof(xmlSecXPathData)=%d",
		    sizeof(xmlSecXPathData));
	return(NULL);
    }
    memset(data, 0, sizeof(xmlSecXPathData)); 
    
    data->xpathType = xpathType;        
    if((node != NULL) && (xmlSecXPathDataReadNode(data, node) < 0)){
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecXPathDataReadNode");
	xmlSecXPathDataDestroy(data);    
	return(NULL);	
    }
    
    if(prev != NULL) {
	prev->next = data;
    }
    return(data);    
}

/**
 * xmlSecXPathDataDestroy
 * @data:
 *
 */
void				
xmlSecXPathDataDestroy(xmlSecXPathDataPtr data) {
    xmlSecXPathDataPtr 	tmp;
    
    while((tmp = data) != NULL) {
	data = data->next;
        if(tmp->expr != NULL) {
	    xmlFree(tmp->expr);
        }
	if(tmp->nsList != NULL) {
	    size_t i;
		
	    for(i = 0; i < tmp->nsListSize; ++i) {
		if((tmp->nsList)[i] != NULL) {
	    	    xmlFree((tmp->nsList)[i]);
		}
	    }
	    memset(tmp->nsList, 0, sizeof(xmlChar*) * (tmp->nsListSize));
	    xmlFree(tmp->nsList);
	}
	memset(tmp, 0, sizeof(xmlSecXPathData));  
        xmlFree(tmp);
    }
}

static int		  
xmlSecXPathDataReadNode	(xmlSecXPathDataPtr data, const xmlNodePtr node) {
    xmlChar *xpath2Type;
    xmlChar* expr;

    xmlSecAssert2(data != NULL, -1);
    xmlSecAssert2(data->expr == NULL, -1);
    xmlSecAssert2(node != NULL, -1);

    expr = xmlNodeGetContent(node);
    if(expr == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_NODE_CONTENT,
		    " ");
        return(-1);
    }
	
    /**
     * Create full XPath expression
     */
    switch(data->xpathType) {
    case xmlSecXPathTypeXPath:
	/* Create full XPath expression */
        data->expr = (xmlChar*) xmlMalloc(sizeof(xmlChar) * 
	        (xmlStrlen(expr) + xmlStrlen(xpathPattern) + 1));
	if(data->expr == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_MALLOC_FAILED,
			"%d",
			xmlStrlen(expr) + xmlStrlen(xpathPattern) + 1);
    	    return(-1);
        }
        sprintf((char*)data->expr, (char*) xpathPattern, expr);	
        xmlFree(expr);
	break;
    case xmlSecXPathTypeXPath2:
	data->expr = expr;
	break;
    case xmlSecXPathTypeXPointer:
	data->expr = expr;
	break;
#ifdef XMLSEC_XPATH2_ALLOW_XPOINTER
    case xmlSecXPathTypeXPointer2:
	data->expr = expr;
	break;
#endif /* XMLSEC_XPATH2_ALLOW_XPOINTER */
    }

    if(xmlSecXPathDataReadNsList(data, node) < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecXPathDataReadNsList");
        return(-1);
    }
    
    switch(data->xpathType) {
    case xmlSecXPathTypeXPath:
    case xmlSecXPathTypeXPointer: 
	/* do nothing */
	break;
    case xmlSecXPathTypeXPath2:
#ifdef XMLSEC_XPATH2_ALLOW_XPOINTER
    case xmlSecXPathTypeXPointer2: 
#endif /* XMLSEC_XPATH2_ALLOW_XPOINTER */
        xpath2Type = xmlGetProp(node, BAD_CAST "Filter");
        if(xpath2Type == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_INVALID_NODE_ATTRIBUTE,
			"Filter not present" );
	    return(-1);
        }

        if(xmlStrEqual(xpath2Type, BAD_CAST "intersect")) {
    	    data->xpath2Type = xmlSecXPathTransformIntersect;
	} else if(xmlStrEqual(xpath2Type, BAD_CAST "subtract")) {
	    data->xpath2Type = xmlSecXPathTransformSubtract;
	} else if(xmlStrEqual(xpath2Type, BAD_CAST "union")) {
	    data->xpath2Type = xmlSecXPathTransformUnion;
	} else {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_INVALID_NODE_ATTRIBUTE,
			"Filter=%s", xpath2Type);
	    xmlFree(xpath2Type);
	    return(-1);
	}
    	xmlFree(xpath2Type);
	break;
    }    
    return(0);
}



static int		  
xmlSecXPathDataReadNsList(xmlSecXPathDataPtr data, const xmlNodePtr node) {
    xmlNodePtr tmp;
    xmlNsPtr ns;
    size_t count;

    xmlSecAssert2(data != NULL, -1);
    xmlSecAssert2(data->nsList == NULL, -1);
    xmlSecAssert2(node != NULL, -1);

    /* how many namespaces we have? */
    count = 0;
    for(tmp = node; tmp != NULL; tmp = tmp->parent) {  
	ns = tmp->nsDef; 
        while(ns != NULL) {	
    	    ++count;
	    ns = ns->next;
	}
    }
    
    data->nsList = (xmlChar**)xmlMalloc(sizeof(xmlChar*) * (2 * count));
    if(data->nsList == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "%d", 2 * count);
	return(-1);
    }    
    data->nsListSize = 2 * count;
    memset(data->nsList, 0, sizeof(xmlChar*) * (data->nsListSize));
    
    count = 0;
    for(tmp = node; tmp != NULL; tmp = tmp->parent) {
	ns = tmp->nsDef;
        while((ns != NULL) && (count < data->nsListSize)){	
	    if(ns->prefix != NULL) {
		data->nsList[count++] = xmlStrdup(ns->prefix);
	    } else {
		data->nsList[count++] = NULL;
	    }	
	    if(ns->href != NULL) {
		data->nsList[count++] = xmlStrdup(ns->href);
	    } else {
		data->nsList[count++] = NULL;
	    }
	    ns = ns->next;
	}
    }
    return(0);
}

static xmlSecNodeSetPtr		  
xmlSecXPathDataExecute(xmlSecXPathDataPtr data, xmlDocPtr doc, xmlNodePtr hereNode) {
    xmlSecNodeSetPtr res = NULL;
    xmlSecNodeSetPtr tmp1, tmp2;
    xmlSecNodeSetOp op;
    xmlSecNodeSetType nodeSetType = xmlSecNodeSetNormal;

    xmlSecAssert2(data != NULL, NULL);
    xmlSecAssert2(data->expr != NULL, NULL);
    xmlSecAssert2(doc != NULL, NULL);
    
    while(data != NULL) {
	xmlXPathObjectPtr xpath = NULL; 
	xmlXPathContextPtr ctx = NULL; 


	switch(data->xpath2Type) {
	case xmlSecXPathTransformIntersect:
	    op = xmlSecNodeSetIntersection;
    	    break;
	case xmlSecXPathTransformSubtract:
	    op = xmlSecNodeSetSubtraction;
	    break;
	case xmlSecXPathTransformUnion:
	    op = xmlSecNodeSetUnion;
	    break;
	default:
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_INVALID_TYPE,
			"xpathType=%d", data->xpath2Type);
	    if(res != NULL) xmlSecNodeSetDestroy(res);
	    return(NULL);
	}

        /**
	 * Create XPath context
	 */
	switch(data->xpathType) {
	case xmlSecXPathTypeXPath:
	case xmlSecXPathTypeXPath2:
	    ctx = xmlXPathNewContext(doc);
	    break;
	case xmlSecXPathTypeXPointer:
#ifdef XMLSEC_XPATH2_ALLOW_XPOINTER
	case xmlSecXPathTypeXPointer2:
#endif /* XMLSEC_XPATH2_ALLOW_XPOINTER */
	    ctx = xmlXPtrNewContext(doc, xmlDocGetRootElement(doc), NULL);
	    break;
	}
        if(ctx == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XML_FAILED,
			"xmlXPathNewContext or xmlXPtrNewContext");
	    if(res != NULL) xmlSecNodeSetDestroy(res);
	    return(NULL);
	}
    
	if(hereNode != NULL) {
	    xmlXPathRegisterFunc(ctx, (xmlChar *)"here", xmlSecXPathHereFunction);
	    ctx->here = hereNode;
	    ctx->xptr = 1;
	}
    
	/*
	 * Register namespaces
         */
	if(data->nsList != NULL) {
	    xmlChar *prefix;
	    xmlChar *href;
	    int i;
		
	    for(i = data->nsListSize - 1; i > 0; ) {
		href = (data->nsList)[i--];
		prefix = (data->nsList)[i--];
	        if((prefix != NULL) && (xmlXPathRegisterNs(ctx, prefix, href) != 0)) {
		    xmlSecError(XMLSEC_ERRORS_HERE,
				XMLSEC_ERRORS_R_XML_FAILED,
				"xmlXPathRegisterNs(%s, %s)", 
				(href != NULL) ? (char*)href : "NULL", 
				(prefix != NULL) ? (char*)prefix : "NULL");
		    xmlXPathFreeContext(ctx); 	     
		    if(res != NULL) xmlSecNodeSetDestroy(res);
		    return(NULL);
		}
	    }
	}

	/*  
         * Evaluate xpath
	 */
	switch(data->xpathType) {
	case xmlSecXPathTypeXPath:
	case xmlSecXPathTypeXPath2:
	    xpath = xmlXPathEvalExpression(data->expr, ctx);
	    break;
	case xmlSecXPathTypeXPointer:
#ifdef XMLSEC_XPATH2_ALLOW_XPOINTER
	case xmlSecXPathTypeXPointer2:
#endif /* XMLSEC_XPATH2_ALLOW_XPOINTER */
	    xpath = xmlXPtrEval(data->expr, ctx);
	    break;
	}
	if(xpath == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XML_FAILED,
			"xmlXPathEvalExpression or xmlXPtrEval");
	    xmlXPathFreeContext(ctx); 
	    if(res != NULL) xmlSecNodeSetDestroy(res);
    	    return(NULL);
	}

	/* store nodes set */
	switch(data->xpathType) {
	case xmlSecXPathTypeXPath:
	    nodeSetType = xmlSecNodeSetNormal;
	    break;
	case xmlSecXPathTypeXPath2:
	    nodeSetType = xmlSecNodeSetTree;
	    break;
	case xmlSecXPathTypeXPointer:
	    nodeSetType = xmlSecNodeSetTree;
	    break;
#ifdef XMLSEC_XPATH2_ALLOW_XPOINTER
	case xmlSecXPathTypeXPointer2:
	    nodeSetType = xmlSecNodeSetTree;
	    break;
#endif /* XMLSEC_XPATH2_ALLOW_XPOINTER */
	}
	tmp1 = xmlSecNodeSetCreate(doc, xpath->nodesetval, nodeSetType);
	if(tmp1 == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecNodeSetCreate");
	    xmlXPathFreeObject(xpath);     
	    xmlXPathFreeContext(ctx); 
	    if(res != NULL) xmlSecNodeSetDestroy(res);
    	    return(NULL);
	}
        xpath->nodesetval = NULL;

	tmp2 = xmlSecNodeSetAdd(res, tmp1, op);
	if(tmp2 == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecNodeSetAdd");
	    xmlSecNodeSetDestroy(tmp1);
	    xmlXPathFreeObject(xpath);     
	    xmlXPathFreeContext(ctx); 
	    if(res != NULL) xmlSecNodeSetDestroy(res);
    	    return(NULL);
	}
	res = tmp2;
	
	/* free everything we do not need */
	xmlXPathFreeObject(xpath);     
	xmlXPathFreeContext(ctx);      

	data = data->next;
    }
    return(res);    
}


