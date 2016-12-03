/*
 *   Copyright (c) 2016, Colomban Wendling <ban@herbesfolles.org>
 *
 *   This source code is released for free distribution under the terms of the
 *   GNU General Public License version 2 or (at your option) any later version.
 *
 *   This module contains functions for generating tags for Swift language
 *   files.
 */

#include "general.h"  /* must always come first */

#include <string.h>

#include "entry.h"
#include "nestlevel.h"
#include "read.h"
#include "main.h"
#include "parse.h"
#include "vstring.h"
#include "keyword.h"
#include "routines.h"
#include "debug.h"
#include "xtag.h"
#include "objpool.h"

#define isIdentifierChar(c) \
	(isalnum (c) || (c) == '_' || (c) >= 0x80)
#define newToken() (objPoolGet (TokenPool))
#define deleteToken(t) (objPoolPut (TokenPool, (t)))

enum {
	KEYWORD_class,
	KEYWORD_deinit,
	KEYWORD_if,
	KEYWORD_init,
	KEYWORD_let,
	KEYWORD_func,
	KEYWORD_typealias,
	KEYWORD_var,
};
typedef int keywordId; /* to allow KEYWORD_NONE */

typedef enum {
	K_CLASS,
	K_FUNCTION,
	K_MEMBER,
	K_VARIABLE,
	K_NAMESPACE,
	K_CONSTANT,
	K_PARAMETER,
	K_LOCAL_VARIABLE,
	K_TYPEALIAS,
	COUNT_KIND
} swiftKind;

static kindOption SwiftKinds[COUNT_KIND] = {
	{true,  'c', "class",       "classes" },
	{true,  'f', "function",    "functions" },
	{true,  'm', "member",      "class members" },
	{true,  'v', "variable",    "variables" },
	{true,  'n', "namespace",   "namespaces" },
	{true,  'C', "constant",    "constants" },
	{false, 'z', "parameter",   "function parameters" },
	{false, 'l', "local",       "local variables" },
	{true,  't', "typealias",   "type alias" },
};

static const keywordTable SwiftKeywordTable[] = {
	/* keyword			keyword ID */
	{ "class",			KEYWORD_class			},
	{ "deinit",			KEYWORD_deinit			},
	{ "if",				KEYWORD_if				},
	{ "init",			KEYWORD_init			},
	{ "let",			KEYWORD_let				},
	{ "func",			KEYWORD_func			},
	{ "typealias",		KEYWORD_typealias		},
	{ "var",			KEYWORD_var				},
};

typedef enum eTokenType {
	/* 0..255 are the byte's value */
	TOKEN_EOF = 256,
	TOKEN_UNDEFINED,
	TOKEN_KEYWORD,
	TOKEN_IDENTIFIER,
	TOKEN_STRING,
	TOKEN_INTEGER,
	TOKEN_REAL,
	TOKEN_RIGHT_ARROW,
	TOKEN_WHITESPACE,
} tokenType;

typedef struct {
	int				type;
	keywordId		keyword;
	vString *		string;
	unsigned long 	lineNumber;
	MIOPos			filePosition;
} tokenInfo;

struct swiftNestingLevelUserData {
	int dummy;
};
#define SWIFT_NL(nl) ((struct swiftNestingLevelUserData *) nestingLevelGetUserData (nl))

static langType Lang_swift;
static NestingLevels *SwiftNestingLevels = NULL;
static objPool *TokenPool = NULL;


static void initSwiftEntry (tagEntryInfo *const e, const tokenInfo *const token,
                            const swiftKind kind)
{
	NestingLevel *nl;

	initTagEntry (e, vStringValue (token->string), &(SwiftKinds[kind]));

	e->lineNumber	= token->lineNumber;
	e->filePosition	= token->filePosition;

	nl = nestingLevelsGetCurrent (SwiftNestingLevels);
	if (nl)
	{
		tagEntryInfo *nlEntry = getEntryOfNestingLevel (nl);

		e->extensionFields.scopeIndex = nl->corkIndex;

		/* nlEntry can be NULL if a kind was disabled.  But what can we do
		 * here?  Even disabled kinds should count for the hierarchy I
		 * guess -- as it'd otherwise be wrong -- but with cork we're
		 * fucked up as there's nothing to look up.  Damn. */
		if (nlEntry && kind == K_VARIABLE)
		{
			int parentKind = (int) (nlEntry->kind - SwiftKinds);

			/* functions directly inside classes are methods, fix it up */
			if (parentKind == K_CLASS)
				e->kind = &(SwiftKinds[K_MEMBER]);
			else
				e->kind = &(SwiftKinds[K_LOCAL_VARIABLE]);
		}
	}
}

static int makeClassTag (const tokenInfo *const token,
                         const vString *const inheritance)
{
	if (SwiftKinds[K_CLASS].enabled)
	{
		tagEntryInfo e;

		initSwiftEntry (&e, token, K_CLASS);

		e.extensionFields.inheritance = inheritance ? vStringValue (inheritance) : "";

		return makeTagEntry (&e);
	}

	return CORK_NIL;
}

static int makeFunctionTag (const tokenInfo *const token,
                            const vString *const arglist,
                            const vString *const type)
{
	if (SwiftKinds[K_FUNCTION].enabled)
	{
		tagEntryInfo e;

		initSwiftEntry (&e, token, K_FUNCTION);

		if (arglist)
			e.extensionFields.signature = vStringValue (arglist);
		if (type)
		{
			e.extensionFields.typeRef[0] = "typename";
			e.extensionFields.typeRef[1] = vStringValue (type);
		}

		return makeTagEntry (&e);
	}

	return CORK_NIL;
}

static int makeVariableTag (const tokenInfo *const token,
                            swiftKind const kind,
                            const vString *const type)
{
	if (SwiftKinds[kind].enabled)
	{
		tagEntryInfo e;

		initSwiftEntry (&e, token, kind);

		if (type)
		{
			e.extensionFields.typeRef[0] = "typename";
			e.extensionFields.typeRef[1] = vStringValue (type);
		}

		return makeTagEntry (&e);
	}

	return CORK_NIL;
}

static int makeTypeAliasTag (const tokenInfo *const token,
                             const vString *const alias)
{
	if (SwiftKinds[K_TYPEALIAS].enabled)
	{
		tagEntryInfo e;

		initSwiftEntry (&e, token, K_TYPEALIAS);

		if (alias)
		{
			e.extensionFields.typeRef[0] = "typename";
			e.extensionFields.typeRef[1] = vStringValue (alias);
		}

		return makeTagEntry (&e);
	}

	return CORK_NIL;
}

static int makeSimpleSwiftTag (const tokenInfo *const token, swiftKind const kind)
{
	if (SwiftKinds[kind].enabled)
	{
		tagEntryInfo e;

		initSwiftEntry (&e, token, kind);
		return makeTagEntry (&e);
	}

	return CORK_NIL;
}

static void *newPoolToken (void)
{
	tokenInfo *token = xMalloc (1, tokenInfo);
	token->string = vStringNew ();
	return token;
}

static void deletePoolToken (void *data)
{
	tokenInfo *token = data;
	vStringDelete (token->string);
	eFree (token);
}

static void clearPoolToken (void *data)
{
	tokenInfo *token = data;

	token->type			= TOKEN_UNDEFINED;
	token->keyword		= KEYWORD_NONE;
	token->lineNumber	= getInputLineNumber ();
	token->filePosition	= getInputFilePosition ();
	vStringClear (token->string);
}

static void copyToken (tokenInfo *const dest, const tokenInfo *const src)
{
	dest->lineNumber	= src->lineNumber;
	dest->filePosition	= src->filePosition;
	dest->type			= src->type;
	dest->keyword		= src->keyword;
	vStringCopy (dest->string, src->string);
}

/* Skip a single or double quoted string.
 * FIXME: check syntax rules */
static void readString (vString *const string, const int delimiter)
{
	int escaped = 0;
	int c;

	while ((c = getcFromInputFile ()) != EOF)
	{
		if (escaped)
		{
			vStringPut (string, c);
			escaped--;
		}
		else if (c == '\\')
			escaped++;
		else if (c == delimiter || c == '\n' || c == '\r')
		{
			if (c != delimiter)
				ungetcToInputFile (c);
			break;
		}
		else
			vStringPut (string, c);
	}
}

static void readIdentifier (vString *const string, const int firstChar)
{
	int c = firstChar;
	if (c == '`')
		c = getcFromInputFile ();
	do
	{
		vStringPut (string, (char) c);
		c = getcFromInputFile ();
	}
	while (isIdentifierChar (c));
	if (c != '`')
		ungetcToInputFile (c);
}

static void readTokenFull (tokenInfo *const token, bool inclWhitespaces)
{
	int c;
	int n;

	token->type		= TOKEN_UNDEFINED;
	token->keyword	= KEYWORD_NONE;
	vStringClear (token->string);

getNextChar:

	n = 0;
	do
	{
		c = getcFromInputFile ();
		n++;
	}
	while (c == ' ' || c == '\t' || c == '\f');

	token->lineNumber   = getInputLineNumber ();
	token->filePosition = getInputFilePosition ();

	if (inclWhitespaces && n > 1 && c != '\r' && c != '\n')
	{
		ungetcToInputFile (c);
		vStringPut (token->string, ' ');
		token->type = TOKEN_WHITESPACE;
		return;
	}

	switch (c)
	{
		case EOF:
			token->type = TOKEN_EOF;
			break;

		case '-':
		{
			int d = getcFromInputFile ();
			vStringPut (token->string, c);
			if (d == '>')
			{
				vStringPut (token->string, d);
				token->type = TOKEN_RIGHT_ARROW;
			}
			else
			{
				ungetcToInputFile (d);
				token->type = c;
			}
			break;
		}

		case '\'':
		case '"':
		{
			token->type = TOKEN_STRING;
			vStringPut (token->string, c);
			readString (token->string, c);
			vStringPut (token->string, c);
			token->lineNumber = getInputLineNumber ();
			token->filePosition = getInputFilePosition ();
			break;
		}

		case '/':
		{
			int d = getcFromInputFile ();
			if (d == '/')
			{
				do
					d = getcFromInputFile();
				while (d != EOF && d != '\r' && d != '\n');
				if (d != EOF)
					ungetcToInputFile (d);
				goto getNextChar;
			}
			else if (d == '*')
			{
				int depth = 1;

				while (d != EOF && depth > 0)
				{
					d = getcFromInputFile();
					if (d == '*')
					{
						d = getcFromInputFile();
						if (d == '/')
							depth--;
						else if (d == '*')
							ungetcToInputFile (d);
					}
					else if (d == '/')
					{
						d = getcFromInputFile();
						if (d == '*')
							depth++;
						else if (d == '/')
							ungetcToInputFile (d);
					}
				}
				goto getNextChar;
			}
			else
			{
				ungetcToInputFile (d);
				vStringPut (token->string, c);
				token->type = c;
			}
			break;
		}

		case '\r': /* newlines for indent */
		case '\n':
		{
			do
			{
				if (c == '\r')
				{
					int d = getcFromInputFile ();
					if (d != '\n')
						ungetcToInputFile (d);
				}
				while ((c = getcFromInputFile ()) == ' ' || c == '\t' || c == '\f')
					;
			} /* skip completely empty lines, so retry */
			while (c == '\r' || c == '\n');
			ungetcToInputFile (c);
			token->type = ';';
			break;
		}

		default:
			if (isdigit (c) || c == '.')
			{
				/* FIXME: hex and stuff? */
				int d = getcFromInputFile ();
				token->type = c == '.' ? TOKEN_REAL : TOKEN_INTEGER;

				vStringPut (token->string, c);
				if (c == '.' && ! isdigit (d))
				{
					token->type = c;
					ungetcToInputFile (d);
				}
				else
				{
					while (isalnum (d) || d == '_' || d == '.' ||
					       d == '-' || d == '+')
					{
						if (d == '.')
						{
							if (token->type == TOKEN_INTEGER)
								token->type = TOKEN_REAL;
							else
								break;
						}
						else if (d == 'p' || d == 'P')
							token->type = TOKEN_REAL;
						else if ((d == '-' || d == '+') &&
						         (token->type != TOKEN_REAL ||
						          (c != 'e' && c != 'E' &&
						           c != 'p' && c != 'P')))
							break;
						vStringPut (token->string, d);
						c = d;
						d = getcFromInputFile ();
					}
					ungetcToInputFile (d);
				}
			}
			else if (! isIdentifierChar (c) && c != '`')
			{
				vStringPut (token->string, c);
				token->type = c;
			}
			else
			{
				readIdentifier (token->string, c);
				token->keyword = lookupKeyword (vStringValue (token->string), Lang_swift);
				if (token->keyword == KEYWORD_NONE)
					token->type = TOKEN_IDENTIFIER;
				else
					token->type = TOKEN_KEYWORD;
			}
			break;
	}
}

static void readToken (tokenInfo *const token)
{
	readTokenFull (token, false);
}

/*================================= parsing =================================*/


static void reprCat (vString *const repr, const tokenInfo *const token)
{
	if (token->type != ';' &&
	    token->type != TOKEN_WHITESPACE)
	{
		vStringCat (repr, token->string);
	}
	else if (vStringLength (repr) > 0 && vStringLast (repr) != ' ')
	{
		vStringPut (repr, ' ');
	}
}

static bool skipOverPair (tokenInfo *const token, int tOpen, int tClose,
                          vString *const repr, bool reprOuterPair)
{
	if (token->type == tOpen)
	{
		int depth = 1;

		if (repr && reprOuterPair)
			reprCat (repr, token);
		do
		{
			readTokenFull (token, true);
			if (repr && (reprOuterPair || token->type != tClose || depth > 1))
				reprCat (repr, token);
			if (token->type == tOpen)
				depth ++;
			else if (token->type == tClose)
				depth --;
		}
		while (token->type != TOKEN_EOF && depth > 0);
	}

	return token->type == tClose;
}

#if 0
static bool skipLambdaArglist (tokenInfo *const token, vString *const repr)
{
	while (token->type != TOKEN_EOF && token->type != ':' &&
	       /* avoid reading too much, just in case */
	       token->type != TOKEN_INDENT)
	{
		bool readNext = true;

		if (token->type == '(')
			readNext = skipOverPair (token, '(', ')', repr, true);
		else if (token->type == '[')
			readNext = skipOverPair (token, '[', ']', repr, true);
		else if (token->type == '{')
			readNext = skipOverPair (token, '{', '}', repr, true);
		else if (token->keyword == KEYWORD_lambda)
		{ /* handle lambdas in a default value */
			if (repr)
				reprCat (repr, token);
			readTokenFull (token, true);
			readNext = skipLambdaArglist (token, repr);
			if (token->type == ':')
				readNext = true;
			if (readNext && repr)
				reprCat (repr, token);
		}
		else if (repr)
		{
			reprCat (repr, token);
		}

		if (readNext)
			readTokenFull (token, true);
	}
	return false;
}
#endif

typedef enum {
	T_NONE    = 0,		// failed to infer
	T_INT     = 1<<0,
	T_DOUBLE  = 1<<1,
	T_STRING  = 1<<2,
	T_UNKNOWN = 0xff,	// don't know yet
} inferedType;

static inferedType inferTypeFromToken (const tokenInfo *const token)
{
	if (token->type == TOKEN_STRING ||
	    (token->type == TOKEN_IDENTIFIER &&
	     strcmp (vStringValue (token->string), "String") == 0))
		return T_STRING;
	else if (token->type == TOKEN_INTEGER)
		return T_INT;
	else if (token->type == TOKEN_REAL)
		return T_DOUBLE;

	return T_UNKNOWN;
}

/* FIXME: avoid possible false positives, like:
 * "hello".method
 * 
 * FIXME: "as":
 * 	var foo = 42 as Float
 * 
 * FIXME: Array:
 * 	var foo = [1, 2, 3]
 * 	var bar = [String]()
 * Dictionary:
 * 	var foo = ["1":"2", "2":"3"]
 * 	var bar = [String: String]()
 */
static bool inferTypeFromExpression (tokenInfo *const token,
                                     vString **const type)
{
	inferedType expressionType = T_UNKNOWN;
	tokenType prevTokenType = TOKEN_UNDEFINED;

	while (token->type != TOKEN_EOF && token->type != ';' && token->type != '{')
	{
		/* skip over function calls/constructors */
		if (token->type == '(' && prevTokenType == TOKEN_IDENTIFIER)
			skipOverPair (token, '(', ')', NULL, false);
		else if (token->type == '.')
		{
			/* FIXME: method call/member access, we can't know the return type */
			expressionType = T_NONE;
		}
		else
			expressionType &= inferTypeFromToken (token);

		prevTokenType = token->type;
		readToken (token);
	}

	switch (expressionType)
	{
		case T_INT:		*type = vStringNewInit ("Int"); break;
		case T_DOUBLE:	*type = vStringNewInit ("Double"); break;
		case T_STRING:	*type = vStringNewInit ("String"); break;
		default: break;
	}

	return false;
}

/* FIXME: how are arrays and dict returned?  [type] I think
 * TODO: handle "Type..."
 * TODO: Array<String>
 * */
static bool readType (tokenInfo *const token,
                      vString **const type)
{
	bool readNext = true;

	if (token->type == '(')
	{
		*type = vStringNew ();
		if (skipOverPair (token, '(', ')', *type, true))
			readToken (token);
		if (token->type != TOKEN_RIGHT_ARROW)
			readNext = false;
		else
		{
			vString *sub = NULL;
			vStringCatS (*type, " -> ");
			readToken (token);
			readNext = readType (token, &sub);
			if (sub)
			{
				vStringCat (*type, sub);
				vStringDelete (sub);
			}
		}
	}
	else
	{
		if (token->type != TOKEN_IDENTIFIER)
			readNext = false;
		else
		{
			/* FIXME: qualified type Foo.Bar
			 * FIXME: Generics */
			*type = vStringNewCopy (token->string);
			readToken (token);
			/* FIXME: handle "Protocol1 & Protocol2" */
			/*if (token->type == '&')
				*/
		}
	}

	/* FIXME: check it's OK with tuple/functions above, like
	 * 	(Int) -> Int!
	 * This will try to read a '?' or '!' a second time, and might be a problem?
	 */
	if (readNext)
		readToken (token);
	if (*type != NULL && (token->type == '?' || token->type == '!'))
		vStringPut (*type, token->type);
	else
		readNext = false;

	return readNext;
}

static void enterScope (tokenInfo *const token, bool root, int parentIndex)
{
	int corkIndex = CORK_NIL;

	readToken (token);
	while (token->type != TOKEN_EOF && (root || token->type != '}'))
	{
		bool readNext = true;
		bool isIf = false;

		/* skip `if` to handle `if let ...` */
		if (token->keyword == KEYWORD_if)
		{
			readToken (token);
			isIf = true;
			corkIndex = CORK_NIL;
		}

		if (token->keyword == KEYWORD_let ||
		    token->keyword == KEYWORD_var)
		{
			swiftKind kind = token->keyword == KEYWORD_let ? K_CONSTANT : K_VARIABLE;

			readToken (token);
			if (token->type == TOKEN_IDENTIFIER)
			{
				vString *type = NULL;
				tokenInfo *const name = newToken ();
				copyToken (name, token);

				readToken (token);
				if (token->type == ':')
				{
					readToken (token);
					readNext = readType (token, &type);
				}
				else if (false &&token->type == '=')
				{
					/* try and infer type from expression */
					readToken (token);
					readNext = inferTypeFromExpression (token, &type);
				}
				else
					readNext = false;

				corkIndex = makeVariableTag (name, kind, type);
				if (isIf)
					corkIndex = CORK_NIL;
				deleteToken (name);
				if (type)
					vStringDelete (type);
			}
			else
				readNext = false;
		}
		else if (token->keyword == KEYWORD_func ||
		         token->keyword == KEYWORD_init ||
		         token->keyword == KEYWORD_deinit)
		{
			if (token->keyword == KEYWORD_func)
				readToken (token);
			if (token->keyword == KEYWORD_func &&
			    token->type != TOKEN_IDENTIFIER)
				readNext = false;
			else
			{
				vString *type = NULL;
				vString *arglist = vStringNew ();
				tokenInfo *const name = newToken ();
				copyToken (name, token);

				readToken (token);
				// FIXME: emit arguments
				if (skipOverPair (token, '(', ')', arglist, true))
					readToken (token);
				if (token->type == TOKEN_RIGHT_ARROW)
				{
					readToken (token);
					readNext = readType (token, &type);
				}
				else
					readNext = false;

				//~ fprintf(stderr, "%s %s -> %s\n",
				        //~ vStringValue (name->string),
				        //~ vStringValue (arglist),
				        //~ type ? vStringValue (type) : "<unknown>");
				corkIndex = makeFunctionTag (name, arglist, type);
				deleteToken (name);
				vStringDelete (arglist);
				if (type)
					vStringDelete (type);
			}
		}
		else if (token->keyword == KEYWORD_class)
		{
			readToken (token);
			if (token->type != TOKEN_IDENTIFIER)
				readNext = false;
			else
			{
				vString *inheritance = vStringNew ();
				tokenInfo *const name = newToken ();
				copyToken (name, token);

				readToken (token);
				if (token->type == ':')
				{
					do
					{
						readToken (token);
						if (token->type == TOKEN_IDENTIFIER ||
						    token->keyword == KEYWORD_class)
						{
							if (vStringLength (inheritance) > 0)
								vStringCatS (inheritance, ", ");
							vStringCat (inheritance, token->string);
						}
						else
							break;
					}
					while (token->type == ',');
				}
				readNext = false;

				corkIndex = makeClassTag (name, inheritance);
				deleteToken (name);
				vStringDelete (inheritance);
			}
		}
		else if (token->keyword == KEYWORD_typealias)
		{
			readToken (token);
			if (token->type != TOKEN_IDENTIFIER)
				readNext = false;
			else
			{
				vString *type = NULL;
				tokenInfo *const name = newToken ();
				copyToken (name, token);

				readToken (token);
				if (token->type == '=')
				{
					readToken (token);
					readNext = readType (token, &type);
				}
				else
					readNext = false;

				corkIndex = makeTypeAliasTag (name, type);
				deleteToken (name);
				if (type)
					vStringDelete (type);
			}
		}
		else if (token->type == '{')
		{
			/* FIXME: be more robust on what scopes are applied to */
			
			if (corkIndex != CORK_NIL)
				nestingLevelsPush (SwiftNestingLevels, corkIndex);
			enterScope (token, false, corkIndex);
			if (corkIndex != CORK_NIL)
				nestingLevelsPop (SwiftNestingLevels);
			corkIndex = CORK_NIL;
			readNext = token->type != TOKEN_EOF;
		}

		if (readNext)
			readToken (token);
	}

	if (parentIndex != CORK_NIL)
	{
		/* attach end: */
		
	}
}

static void findSwiftTags (void)
{
	tokenInfo *const token = newToken ();

	SwiftNestingLevels = nestingLevelsNew (sizeof (struct swiftNestingLevelUserData));

	enterScope (token, true, CORK_NIL);

	nestingLevelsFree (SwiftNestingLevels);
	deleteToken (token);
}

static void initialize (const langType language)
{
	Lang_swift = language;

	TokenPool = objPoolNew (16, newPoolToken, deletePoolToken, clearPoolToken);
}

static void finalize (langType language CTAGS_ATTR_UNUSED, bool initialized)
{
	if (!initialized)
		return;

	objPoolDelete (TokenPool);
}

extern parserDefinition* SwiftParser (void)
{
	static const char *const extensions[] = { "swift", NULL };
	parserDefinition *def = parserNew ("Swift");
	def->kinds = SwiftKinds;
	def->kindCount = ARRAY_SIZE (SwiftKinds);
	def->extensions = extensions;
	def->parser = findSwiftTags;
	def->initialize = initialize;
	def->finalize = finalize;
	def->keywordTable = SwiftKeywordTable;
	def->keywordCount = ARRAY_SIZE (SwiftKeywordTable);
	def->useCork = true;
	def->requestAutomaticFQTag = true;
	return def;
}
