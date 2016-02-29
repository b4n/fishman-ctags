/*
*   Copyright (c) 2016, Szymon Tomasz Stefanek
*
*   This source code is released for free distribution under the terms of the
*   GNU General Public License version 2 or (at your option) any later version.
*
*   This module contains functions for parsing and scanning C++ source files
*/
#include "cxx_parser.h"
#include "cxx_parser_internal.h"

#include "cxx_debug.h"
#include "cxx_keyword.h"
#include "cxx_token.h"
#include "cxx_token_chain.h"
#include "cxx_scope.h"

#include "parse.h"
#include "vstring.h"
#include "get.h"
#include "debug.h"
#include "keyword.h"
#include "read.h"

#include <string.h>

//
// The global parser state
//
CXXParserState g_cxx;

//
// This is set to false once the parser is run at least one time. Used by cleanup routines.
//
boolean g_bFirstRun = TRUE;

//
// Reset parser state:
// - Clear the token chain
// - Reset "seen" keywords
//
void cxxParserNewStatement()
{
	cxxTokenChainClear(g_cxx.pTokenChain);
	if(g_cxx.pTemplateTokenChain)
	{
		cxxTokenChainDestroy(g_cxx.pTemplateTokenChain);
		g_cxx.pTemplateTokenChain = NULL;
	}
	g_cxx.uKeywordState = 0;

	cppEndStatement(); // FIXME: this cpp handling is broken: it works only because the moon is in the correct phase.

	//g_cxx.bParsingTemplateAngleBrackets = FALSE; <-- no need for this, it's always reset to false after usage
	//g_cxx.bParsingClassStructOrUnionDeclaration = FALSE; <-- ditto
}

//
// Parse a subchain of input delimited by matching pairs: [],(),{},<> [WARNING: no other subchain types are recognized!]
// This function expects g_cxx.pToken to point to the initial token of the pair ([{<.
// It will parse input until the matching terminator token is found.
// Inner parsing is done by cxxParserParseAndCondenseSubchainsUpToOneOf()
// so this is actually a recursive subchain nesting algorithm.
//
boolean cxxParserParseAndCondenseCurrentSubchain(
		unsigned int uInitialSubchainMarkerTypes,
		boolean bAcceptEOF
	)
{
	CXXTokenChain * pCurrentChain = g_cxx.pTokenChain;
	g_cxx.pTokenChain = cxxTokenChainCreate();
	CXXToken * pInitial = cxxTokenChainTakeLast(pCurrentChain);
	cxxTokenChainAppend(g_cxx.pTokenChain,pInitial);
	CXXToken * pChainToken = cxxTokenCreate();
	pChainToken->eType = (enum CXXTokenType)(g_cxx.pToken->eType << 8); // see the declaration of CXXTokenType enum. Shifting by 8 gives the corresponding chain marker
	pChainToken->pChain = g_cxx.pTokenChain;
	cxxTokenChainAppend(pCurrentChain,pChainToken);
	unsigned int uTokenTypes = g_cxx.pToken->eType << 4; // see the declaration of CXXTokenType enum. Shifting by 4 gives the corresponding closing token type
	if(bAcceptEOF)
		uTokenTypes |= CXXTokenTypeEOF;
	boolean bRet = cxxParserParseAndCondenseSubchainsUpToOneOf(
			uTokenTypes,
			uInitialSubchainMarkerTypes
		);
	g_cxx.pTokenChain = pCurrentChain;
	g_cxx.pToken = pCurrentChain->pTail;
	return bRet;
}

//
// This function parses input until one of the specified tokens appears.
// The algorithm will also build subchains of matching pairs ([...],(...),<...>,{...}): within the subchain
// analysis of uTokenTypes is completly disabled. Subchains do nest.
// 
// Returns true if it stops before EOF or it stops at EOF and CXXTokenTypeEOF is present in uTokenTypes.
// Returns false in all the other stop conditions and when an unmatched subchain character pair is found (syntax error).
//
boolean cxxParserParseAndCondenseSubchainsUpToOneOf(
		unsigned int uTokenTypes,
		unsigned int uInitialSubchainMarkerTypes
	)
{
	CXX_DEBUG_ENTER_TEXT("Token types = 0x%x",uTokenTypes);
	if(!cxxParserParseNextToken())
	{
		CXX_DEBUG_LEAVE_TEXT("Found EOF");
		return (uTokenTypes & CXXTokenTypeEOF); // was already at EOF
	}

	unsigned int uFinalSubchainMarkerTypes = uInitialSubchainMarkerTypes << 4;  // see the declaration of CXXTokenType enum. Shifting by 4 gives the corresponding closing token type

	for(;;)
	{
		//CXX_DEBUG_PRINT("Current token is '%s' 0x%x",vStringValue(g_cxx.pToken->pszWord),g_cxx.pToken->eType);

		if(g_cxx.pToken->eType & uTokenTypes)
		{
			CXX_DEBUG_LEAVE_TEXT("Got terminator token '%s' 0x%x",vStringValue(g_cxx.pToken->pszWord),g_cxx.pToken->eType);
			return TRUE;
		}
		
		if(g_cxx.pToken->eType & uInitialSubchainMarkerTypes)
		{
			// subchain
			CXX_DEBUG_PRINT("Got subchain start token '%s' 0x%x",vStringValue(g_cxx.pToken->pszWord),g_cxx.pToken->eType);
			CXXToken * pParenthesis;

			if(
				(g_cxx.pToken->eType & CXXTokenTypeOpeningBracket) &&
				cxxParserCurrentLanguageIsCPP() &&
				(pParenthesis = cxxParserOpeningBracketIsLambda())
			)
			{
				if(!cxxParserHandleLambda(pParenthesis))
				{
					CXX_DEBUG_LEAVE_TEXT("Failed to handle lambda");
					return FALSE;
				}
			} else {
				if(!cxxParserParseAndCondenseCurrentSubchain(uInitialSubchainMarkerTypes,(uTokenTypes & CXXTokenTypeEOF)))
				{
					CXX_DEBUG_LEAVE_TEXT("Failed to parse subchain of type 0x%x",g_cxx.pToken->eType);
					return false;
				}
			}

			if(g_cxx.pToken->eType & uTokenTypes)
			{
				// was looking for a subchain
				CXX_DEBUG_LEAVE_TEXT("Got terminator subchain token 0x%x",g_cxx.pToken->eType);
				return TRUE;
			}

			if(!cxxParserParseNextToken())
			{
				CXX_DEBUG_LEAVE_TEXT("Found EOF(2)");
				return (uTokenTypes & CXXTokenTypeEOF); // was already at EOF
			}

			continue; // jump up to avoid checking for mismatched pairs below
		}

		// Check for mismatched brackets/parenthis
		// Note that if we were looking for one of [({ then we would have matched it at the top of the for
		if(g_cxx.pToken->eType & uFinalSubchainMarkerTypes)
		{
			CXX_DEBUG_LEAVE_TEXT("Got mismatched subchain terminator 0x%x",g_cxx.pToken->eType);
			return FALSE; // unmatched: syntax error
		}

		if(!cxxParserParseNextToken())
		{
			CXX_DEBUG_LEAVE_TEXT("Found EOF(3)");
			return (uTokenTypes & CXXTokenTypeEOF); // was already at EOF
		}
	}

	// not reached
	CXX_DEBUG_LEAVE_TEXT("Internal error");
	return FALSE;
}

//
// This function parses input until one of the specified tokens appears.
// The algorithm will also build subchains of matching pairs ([...],(...),{...}): within the subchain
// analysis of uTokenTypes is completly disabled. Subchains do nest.
//
// Please note that this function will skip entire scopes (matching {} pairs) unless
// you pass CXXTokenTypeOpeningBracket to stop at their beginning.
// This is usually what you want, unless you're really expecting a scope to begin in
// the current statement.
//
boolean cxxParserParseUpToOneOf(unsigned int uTokenTypes)
{
	return cxxParserParseAndCondenseSubchainsUpToOneOf(
			uTokenTypes,
			CXXTokenTypeOpeningBracket | CXXTokenTypeOpeningParenthesis | CXXTokenTypeOpeningSquareParenthesis
		);
}

//
// This is called after a full enum/struct/class/union declaration that ends with a closing bracket.
//
static boolean cxxParserParseEnumStructClassOrUnionFullDeclarationTrailer(boolean bParsingTypedef,enum CXXTagKind eTagKind,const char * szTypeName)
{
	CXX_DEBUG_ENTER();

	cxxTokenChainClear(g_cxx.pTokenChain);
	
	CXX_DEBUG_PRINT("Parse enum/struct/class/union trailer, typename is '%s'",szTypeName);

	if(!cxxParserParseUpToOneOf(CXXTokenTypeEOF | CXXTokenTypeSemicolon))
	{
		CXX_DEBUG_LEAVE_TEXT("Failed to parse up to EOF/semicolon");
		return FALSE;
	}

	if(g_cxx.pToken->eType & CXXTokenTypeEOF)
	{
		// It's a syntax error, but we can be tolerant here.
		CXX_DEBUG_LEAVE_TEXT("Got EOF after enum/class/struct/union block");
		return TRUE;
	}

	if(g_cxx.pTokenChain->iCount < 2)
	{
		CXX_DEBUG_LEAVE_TEXT("Nothing interesting after enum/class/struct/union block");
		return TRUE;
	}

	if(bParsingTypedef)
	{
		cxxTokenChainDestroyLast(g_cxx.pTokenChain);

		CXXToken * pLast = cxxTokenChainLast(g_cxx.pTokenChain);
		if(pLast && (pLast->eType == CXXTokenTypeIdentifier))
		{
			tagEntryInfo * tag = cxxTagBegin(
					vStringValue(pLast->pszWord),
					CXXTagKindTYPEDEF,
					pLast
				);
			
			if(tag)
			{
				
				if(szTypeName)
				{
					tag->extensionFields.typeRef[0] = cxxTagGetKindOptions()[eTagKind].name;
					tag->extensionFields.typeRef[1] = szTypeName;
				}
	
				// FIXME: This is quite debatable.
				tag->isFileScope = !isInputHeaderFile();
	
				cxxTagCommit();
			}
		}
		
		CXX_DEBUG_LEAVE_TEXT("Parsed typedef");
		return TRUE;
	}

	// fake the initial two tokens
	CXXToken * pIdentifier = cxxTokenCreate();
	pIdentifier->eType = CXXTokenTypeIdentifier;
	pIdentifier->bFollowedBySpace = TRUE;
	vStringCatS(pIdentifier->pszWord,szTypeName);
	cxxTokenChainPrepend(g_cxx.pTokenChain,pIdentifier);
	
	CXXToken * pKeyword = cxxTokenCreate();
	pKeyword->eType = CXXTokenTypeKeyword;
	pKeyword->bFollowedBySpace = TRUE;
	vStringCatS(pKeyword->pszWord,cxxTagGetKindOptions()[eTagKind].name);
	cxxTokenChainPrepend(g_cxx.pTokenChain,pKeyword);

	cxxParserExtractVariableDeclarations(g_cxx.pTokenChain);

	CXX_DEBUG_LEAVE();
	return TRUE;
}

boolean cxxParserParseEnum()
{
	CXX_DEBUG_ENTER();

	cxxTokenChainClear(g_cxx.pTokenChain);

	/*
		Spec is:
			enum-key attr(optional) identifier(optional) enum-base(optional) { enumerator-list(optional) }	(1)	
			enum-key attr(optional) identifier enum-base(optional) ;	(2)	(since C++11)
	*/

	// Skip attr and class-head-name
	if(!cxxParserParseUpToOneOf(CXXTokenTypeEOF | CXXTokenTypeSemicolon | CXXTokenTypeParenthesisChain | CXXTokenTypeOpeningBracket))
	{
		CXX_DEBUG_LEAVE_TEXT("Could not parse enum name");
		return FALSE;
	}

	if(g_cxx.pToken->eType == CXXTokenTypeParenthesisChain)
	{
		// probably a function declaration/prototype
		// something like enum x func()....
		// do not clear statement
		CXX_DEBUG_LEAVE_TEXT("Probably a function declaration!");
		return TRUE;
	}

	// FIXME: This block is duplicated in struct/union/class
	if(g_cxx.pToken->eType == CXXTokenTypeSemicolon)
	{
		if(g_cxx.pTokenChain->iCount > 3) // [typedef] struct X Y; <-- typedef has been removed!
		{
			if(g_cxx.uKeywordState & CXXParserKeywordStateSeenTypedef)
			{
				if(g_cxx.pToken->pPrev->eType == CXXTokenTypeIdentifier)
				{
					// assume typedef.
					// FIXME: Typeref!
					tagEntryInfo * tag = cxxTagBegin(
							vStringValue(g_cxx.pToken->pPrev->pszWord),
							CXXTagKindTYPEDEF,
							g_cxx.pToken->pPrev
						);
		
					if(tag)
					{
						tag->isFileScope = !isInputHeaderFile();
					
						cxxTagCommit();
					}
				}
			} else {
				cxxParserExtractVariableDeclarations(g_cxx.pTokenChain);
			}
		}

		cxxParserNewStatement();
		CXX_DEBUG_LEAVE();
		return TRUE;
	}
	
	if(g_cxx.pToken->eType == CXXTokenTypeEOF)
	{
		// tolerate EOF, treat as forward declaration
		cxxParserNewStatement();
		CXX_DEBUG_LEAVE_TEXT("EOF before enum block: treating as forward declaration");
		return TRUE;
	}

	// semicolon or opening bracket

	// check if we can extract a class name identifier
	CXXToken * pEnumName = cxxTokenChainLastTokenOfType(g_cxx.pTokenChain,CXXTokenTypeIdentifier);
	
	int iPushedScopes = 0;
	
	if(pEnumName)
	{
		// good.
		// It may be qualified though.
		CXXToken * pNamespaceBegin = pEnumName;
		CXXToken * pPrev = pEnumName->pPrev;
		while(pPrev)
		{
			if(pPrev->eType != CXXTokenTypeMultipleColons)
				break;
			pPrev = pPrev->pPrev;
			if(!pPrev)
				break;
			if(pPrev->eType != CXXTokenTypeIdentifier)
				break;
			pNamespaceBegin = pPrev;
			pPrev = pPrev->pPrev;
		}
		
		while(pNamespaceBegin != pEnumName)
		{
			CXXToken * pNext = pNamespaceBegin->pNext;
			cxxTokenChainTake(g_cxx.pTokenChain,pNamespaceBegin);
			cxxScopePush(pNamespaceBegin,CXXTagKindCLASS,CXXScopeAccessUnknown); // FIXME: We don't really know if it's a class!
			iPushedScopes++;
			pNamespaceBegin = pNext->pNext;
		}
		
		CXX_DEBUG_PRINT("Enum name is %s",vStringValue(pEnumName->pszWord));
		cxxTokenChainTake(g_cxx.pTokenChain,pEnumName);
	} else {
		pEnumName = cxxTokenCreateAnonymousIdentifier();
		CXX_DEBUG_PRINT("Enum name is %s (anonymous)",vStringValue(pEnumName->pszWord));
	}

	tagEntryInfo * tag = cxxTagBegin(pEnumName->pszWord->buffer,CXXTagKindENUM,pEnumName);
	
	if(tag)
	{
		// FIXME: this is debatable
		tag->isFileScope = !isInputHeaderFile();
	
		cxxTagCommit();
	}

	cxxScopePush(pEnumName,CXXTagKindENUM,CXXScopeAccessPublic);
	iPushedScopes++;

	vString * pScopeName = cxxScopeGetFullNameAsString();

	// Special kind of block
	for(;;)
	{
		cxxTokenChainClear(g_cxx.pTokenChain);
	
		if(!cxxParserParseUpToOneOf(CXXTokenTypeComma | CXXTokenTypeClosingBracket | CXXTokenTypeEOF))
		{
			CXX_DEBUG_LEAVE_TEXT("Failed to parse enum contents");
			if(pScopeName)
				vStringDelete(pScopeName);
			return FALSE;
		}

		CXXToken * pFirst = cxxTokenChainFirst(g_cxx.pTokenChain);

		// enumerator.
		if((g_cxx.pTokenChain->iCount > 1) && (pFirst->eType == CXXTokenTypeIdentifier))
		{
			tag = cxxTagBegin(vStringValue(pFirst->pszWord),CXXTagKindENUMERATOR,pFirst);
			if(tag)
			{
				tag->isFileScope = !isInputHeaderFile();
				cxxTagCommit();
			}
		}

		if(g_cxx.pToken->eType & (CXXTokenTypeEOF | CXXTokenTypeClosingBracket))
			break;
	}

	while(iPushedScopes > 0)
	{
		cxxScopePop();
		iPushedScopes--;
	}

	boolean bRet = cxxParserParseEnumStructClassOrUnionFullDeclarationTrailer((g_cxx.uKeywordState & CXXParserKeywordStateSeenTypedef),CXXTagKindENUM,vStringValue(pScopeName));

	if(pScopeName)
		vStringDelete(pScopeName);

	cxxParserNewStatement();
	CXX_DEBUG_LEAVE();
	return bRet;
};


boolean cxxParserParseClassStructOrUnion(enum CXXTagKind eTagKind)
{
	CXX_DEBUG_ENTER();

	//cxxTokenChainClear(g_cxx.pTokenChain);
	
	boolean bParsingTypedef = (g_cxx.uKeywordState & CXXParserKeywordStateSeenTypedef); // may be cleared below

	/*
		Spec is:
			class-key attr class-head-name base-clause { member-specification }		

			class-key	-	one of class or struct. The keywords are identical except for the default member access and the default base class access.
			attr(C++11)	-	optional sequence of any number of attributes, may include alignas specifier
			class-head-name	-	the name of the class that's being defined. Optionally qualified, optionally followed by keyword final. The name may be omitted, in which case the class is unnamed (note that unnamed class cannot be final)
			base-clause	-	optional list of one or more parent classes and the model of inheritance used for each (see derived class)
			member-specification	-	list of access specifiers, member object and member function declarations and definitions (see below)
	*/

	// Skip attr and class-head-name

	g_cxx.bParsingClassStructOrUnionDeclaration = TRUE; // enable "final" keyword handling

	unsigned int uTerminatorTypes = CXXTokenTypeEOF | CXXTokenTypeSingleColon | CXXTokenTypeSemicolon | CXXTokenTypeOpeningBracket | CXXTokenTypeSmallerThanSign;
	if(eTagKind != CXXTagKindCLASS)
		uTerminatorTypes |= CXXTokenTypeParenthesisChain;

	boolean bRet;

	for(;;)
	{
		bRet = cxxParserParseUpToOneOf(uTerminatorTypes);

		if(!bRet)
		{
			g_cxx.bParsingClassStructOrUnionDeclaration = FALSE;
			CXX_DEBUG_LEAVE_TEXT("Could not parse class/struct/union name");
			return FALSE;
		}

		if(g_cxx.pToken->eType != CXXTokenTypeSmallerThanSign)
			break;

		// Probably a template specialisation
		
		// template<typename T> struct X<int>
		// {
		// }
		
		// FIXME: Should we add the specialisation arguments somewhere? Maye as a separate field?

		bRet = cxxParserParseAndCondenseCurrentSubchain(
					CXXTokenTypeOpeningParenthesis | CXXTokenTypeOpeningBracket | CXXTokenTypeOpeningSquareParenthesis | CXXTokenTypeSmallerThanSign,
					FALSE
				);

		if(!bRet)
		{
			g_cxx.bParsingClassStructOrUnionDeclaration = FALSE;
			CXX_DEBUG_LEAVE_TEXT("Could not parse class/struct/union name");
			return FALSE;
		}
	}

	g_cxx.bParsingClassStructOrUnionDeclaration = FALSE;

	if(g_cxx.pToken->eType == CXXTokenTypeParenthesisChain)
	{
		// probably a function declaration/prototype
		// something like struct x * func()....
		// do not clear statement
		CXX_DEBUG_LEAVE_TEXT("Probably a function declaration!");
		return TRUE;
	}

	// FIXME: This block is duplicated in enum
	if(g_cxx.pToken->eType == CXXTokenTypeSemicolon)
	{
		if(g_cxx.pTokenChain->iCount > 3) // [typedef] struct X Y; <-- typedef has been removed!
		{
			if(bParsingTypedef)
			{
				if(g_cxx.pToken->pPrev->eType == CXXTokenTypeIdentifier)
				{
					// assume typedef.
					// FIXME: Typeref!
					tagEntryInfo * tag = cxxTagBegin(
							vStringValue(g_cxx.pToken->pPrev->pszWord),
							CXXTagKindTYPEDEF,
							g_cxx.pToken->pPrev
						);
		
					if(tag)
					{
						tag->isFileScope = !isInputHeaderFile();
					
						cxxTagCommit();
					}
				}
			} else {
				cxxParserExtractVariableDeclarations(g_cxx.pTokenChain);
			}
		}

		cxxParserNewStatement();
		CXX_DEBUG_LEAVE();
		return TRUE;
	}
	
	if(g_cxx.pToken->eType == CXXTokenTypeEOF)
	{
		// tolerate EOF, just ignore this
		cxxParserNewStatement();
		CXX_DEBUG_LEAVE_TEXT("EOF: ignoring");
		return TRUE;
	}

	// semicolon or opening bracket

	// check if we can extract a class name identifier
	CXXToken * pClassName = cxxTokenChainLastTokenOfType(g_cxx.pTokenChain,CXXTokenTypeIdentifier);
	
	int iPushedScopes = 0;
	
	if(pClassName)
	{
		// good.
		// It may be qualified though.
		CXXToken * pNamespaceBegin = pClassName;
		CXXToken * pPrev = pClassName->pPrev;
		while(pPrev)
		{
			if(pPrev->eType != CXXTokenTypeMultipleColons)
				break;
			pPrev = pPrev->pPrev;
			if(!pPrev)
				break;
			if(pPrev->eType != CXXTokenTypeIdentifier)
				break;
			pNamespaceBegin = pPrev;
			pPrev = pPrev->pPrev;
		}
		
		while(pNamespaceBegin != pClassName)
		{
			CXXToken * pNext = pNamespaceBegin->pNext;
			cxxTokenChainTake(g_cxx.pTokenChain,pNamespaceBegin);
			cxxScopePush(pNamespaceBegin,CXXTagKindCLASS,CXXScopeAccessUnknown); // FIXME: We don't really know if it's a class!
			iPushedScopes++;
			pNamespaceBegin = pNext->pNext;
		}
		
		CXX_DEBUG_PRINT("Class/struct/union name is %s",vStringValue(pClassName->pszWord));
		cxxTokenChainTake(g_cxx.pTokenChain,pClassName);
	} else {
		pClassName = cxxTokenCreateAnonymousIdentifier();
		CXX_DEBUG_PRINT("Class/struct/union name is %s (anonymous)",vStringValue(pClassName->pszWord));
	}

	cxxTokenChainClear(g_cxx.pTokenChain);

	if(g_cxx.pToken->eType == CXXTokenTypeSingleColon)
	{
		// check for base classes
	
		if(!cxxParserParseUpToOneOf(CXXTokenTypeEOF | CXXTokenTypeSemicolon | CXXTokenTypeOpeningBracket))
		{
			cxxTokenDestroy(pClassName);
			CXX_DEBUG_LEAVE_TEXT("Failed to parse base class part");
			return FALSE;
		}
	
		if(g_cxx.pToken->eType & (CXXTokenTypeSemicolon | CXXTokenTypeEOF))
		{
			cxxTokenDestroy(pClassName);
			cxxParserNewStatement();
			CXX_DEBUG_LEAVE_TEXT("Syntax error: ignoring");
			return TRUE;
		}
		
		cxxTokenChainDestroyLast(g_cxx.pTokenChain); // remove the {
	}

	tagEntryInfo * tag = cxxTagBegin(pClassName->pszWord->buffer,eTagKind,pClassName);

	if(tag)
	{
		if(g_cxx.pTokenChain->iCount > 0)
		{
			cxxTokenChainCondense(g_cxx.pTokenChain,0);
			tag->extensionFields.inheritance = vStringValue(g_cxx.pTokenChain->pHead->pszWord);
		}
		
		tag->isFileScope = !isInputHeaderFile();
		
		cxxTagCommit();
	}

	cxxScopePush(pClassName,eTagKind,(eTagKind == CXXTagKindCLASS) ? CXXScopeAccessPrivate : CXXScopeAccessPublic);

	vString * pScopeName = cxxScopeGetFullNameAsString();

	if(!cxxParserParseBlock(TRUE))
	{
		CXX_DEBUG_LEAVE_TEXT("Failed to parse scope");
		if(pScopeName)
			vStringDelete(pScopeName);
		return FALSE;
	}

	iPushedScopes++;
	while(iPushedScopes > 0)
	{
		cxxScopePop();
		iPushedScopes--;
	}

	bRet = cxxParserParseEnumStructClassOrUnionFullDeclarationTrailer(bParsingTypedef,eTagKind,vStringValue(pScopeName));

	if(pScopeName)
		vStringDelete(pScopeName);

	cxxParserNewStatement();
	CXX_DEBUG_LEAVE();
	return bRet;
};

//
// This is called at block level, upon encountering a semicolon, an unbalanced closing bracket or EOF.
// The current token is something like:
//   static const char * variable;
//   int i = ....
//   const QString & function(whatever) const;
//   QString szText("ascii");
//   QString(...)
//
// Notable facts:
//   - several special statements never end up here: this includes class, struct, union, enum, namespace, typedef,
//     case, try, catch and other similar stuff.
//   - the terminator is always at the end. It's either a semicolon, a closing bracket or an EOF
//   - the parentheses and brackets are always condensed in subchains (unless unbalanced).
//
//                int __attribute__() function();
//                                  |          |
//                             ("whatever")  (int var1,type var2) 
//
//                const char * strings[] = {}
//                                    |     |
//                                   [10]  { "string","string",.... }
//
// This function tries to extract variable declarations and function prototypes.
//
// Yes, it's complex: it's because C/C++ is complex.
//
void cxxParserAnalyzeOtherStatement()
{
	CXX_DEBUG_ENTER();

#ifdef CXX_DO_DEBUGGING
	vString * pChain = cxxTokenChainJoin(g_cxx.pTokenChain,NULL,0);
	CXX_DEBUG_PRINT("Analyzing statement '%s'",vStringValue(pChain));
	vStringDelete(pChain);
#endif

	CXX_DEBUG_ASSERT(g_cxx.pTokenChain->iCount > 0,"There should be at least the terminator here!");

	if(g_cxx.pTokenChain->iCount < 2)
	{
		CXX_DEBUG_LEAVE_TEXT("Empty statement");
		return;
	}
	
	if(g_cxx.uKeywordState & CXXParserKeywordStateSeenReturn)
	{
		CXX_DEBUG_LEAVE_TEXT("Statement after a return is not interesting");
		return;
	}

	// Everything we can make sense of starts with an identifier or keyword. This is usually a type name
	// (eventually decorated by some attributes and modifiers) with the notable exception of constructor/destructor declarations
	// (which are still identifiers tho).
	
	CXXToken * t = cxxTokenChainFirst(g_cxx.pTokenChain);
	
	if(!(t->eType & (CXXTokenTypeIdentifier | CXXTokenTypeKeyword)))
	{
		CXX_DEBUG_LEAVE_TEXT("Statement does not start with an identifier or keyword");
		return;
	}

	enum CXXTagKind eScopeKind = cxxScopeGetKind();

	int mayBeFunction; // 0 = no, 1 = maybe, 2 = only possibility

	// kinda looks like a function or variable instantiation... maybe
	if(eScopeKind == CXXTagKindFUNCTION)
	{
		// certainly not a function, maybe variable declarations or instantiations (or just other statement)
		mayBeFunction = 0;
	} else {
		if(
			g_cxx.uKeywordState &
			(CXXParserKeywordStateSeenInline | CXXParserKeywordStateSeenExplicit |
			CXXParserKeywordStateSeenOperator | CXXParserKeywordStateSeenVirtual)
		)
			mayBeFunction = 2; // must be function
		else
			mayBeFunction = 1; // might be function but also anything else
	}

	CXXFunctionSignatureInfo oInfo;

	if(mayBeFunction)
	{
		if(cxxParserLookForFunctionSignature(g_cxx.pTokenChain,&oInfo,NULL))
		{
			cxxParserEmitFunctionTags(&oInfo,CXXTagKindPROTOTYPE,0);
			CXX_DEBUG_LEAVE_TEXT("Found function prototype");
			return;
		}
		
		if(mayBeFunction == 2)
		{
			CXX_DEBUG_LEAVE_TEXT("WARNING: Was expecting to find a function prototype but did not find one");
			return;
		}
	}

	cxxParserExtractVariableDeclarations(g_cxx.pTokenChain);
	CXX_DEBUG_LEAVE_TEXT("Nothing else");
}


// This is called when we encounter a "public", "protected" or "private" keyword
// that is NOT in the class declaration header line.
boolean cxxParserParseAccessSpecifier()
{
	CXX_DEBUG_ENTER();

	enum CXXTagKind eScopeKind = cxxScopeGetKind();

	if((eScopeKind != CXXTagKindCLASS) && (eScopeKind != CXXTagKindSTRUCT) && (eScopeKind != CXXTagKindUNION))
	{
		// this is a syntax error: we're in the wrong scope.
		CXX_DEBUG_LEAVE_TEXT("Access specified in wrong context (%d): bailing out to avoid reporting broken structure",eScopeKind);
		return FALSE;
	}
	
	switch(g_cxx.pToken->eKeyword)
	{
		case CXXKeywordPUBLIC:
			cxxScopeSetAccess(CXXScopeAccessPublic);
		break;
		case CXXKeywordPRIVATE:
			cxxScopeSetAccess(CXXScopeAccessPrivate);
		break;
		case CXXKeywordPROTECTED:
			cxxScopeSetAccess(CXXScopeAccessProtected);
		break;
		default:
			CXX_DEBUG_ASSERT(false,"Bad keyword in cxxParserParseAccessSpecifier!");
		break;
	}
	
	// skip to the next :, without leaving scope.
	if(!cxxParserParseUpToOneOf(CXXTokenTypeSingleColon | CXXTokenTypeSemicolon | CXXTokenTypeClosingBracket | CXXTokenTypeEOF))
	{
		CXX_DEBUG_LEAVE_TEXT("Failed to parse up to the next ;");
		return false;
	}

	cxxTokenChainClear(g_cxx.pTokenChain);
	CXX_DEBUG_LEAVE();
	return TRUE;
}


//
// This is used to handle non struct/class/union/enum typedefs.
//
boolean cxxParserParseGenericTypedef()
{
	CXX_DEBUG_ENTER();
	
	for(;;)
	{
		if(!cxxParserParseUpToOneOf(CXXTokenTypeSemicolon | CXXTokenTypeEOF | CXXTokenTypeClosingBracket | CXXTokenTypeKeyword))
		{
			CXX_DEBUG_LEAVE_TEXT("Failed to parse fast statement");
			return FALSE;
		}
		
		// This fixes bug reported by Emil Rojas in 2002.
		// Though it's quite debatable if we really *should* do this.
		if(g_cxx.pToken->eType != CXXTokenTypeKeyword)
		{
			if(g_cxx.pToken->eType != CXXTokenTypeSemicolon)
			{
				CXX_DEBUG_LEAVE_TEXT("Found EOF/closing bracket at typedef");
				return TRUE; // EOF
			}
			
			break;
		}

		if(
			(g_cxx.pToken->eKeyword == CXXKeywordEXTERN) ||
			(g_cxx.pToken->eKeyword == CXXKeywordTYPEDEF) ||
			(g_cxx.pToken->eKeyword == CXXKeywordSTATIC)
		)
		{
			CXX_DEBUG_LEAVE_TEXT("Found a terminating keyword inside typedef");
			return TRUE; // treat as semicolon
		}

	}

	// find the last identifier
	CXXToken * t = cxxTokenChainLastTokenOfType(g_cxx.pTokenChain,CXXTokenTypeIdentifier);
	if(!t)
	{
		CXX_DEBUG_LEAVE_TEXT("Didn't find an identifier");
		return TRUE; // EOF
	}
	
	if(!t->pPrev)
	{
		CXX_DEBUG_LEAVE_TEXT("No type before the typedef'd identifier");
		return TRUE; // EOF
	}

	// FIXME: typeref here?
	tagEntryInfo * tag = cxxTagBegin(
			vStringValue(t->pszWord),
			CXXTagKindTYPEDEF,
			t
		);

	if(tag)
	{
		// This is debatable
		tag->isFileScope = !isInputHeaderFile();
	
		cxxTagCommit();
	}

	CXX_DEBUG_LEAVE();
	return TRUE;
}

boolean cxxParserParseIfForWhileSwitch()
{
	CXX_DEBUG_ENTER();

	if(!cxxParserParseUpToOneOf(CXXTokenTypeParenthesisChain | CXXTokenTypeSemicolon | CXXTokenTypeOpeningBracket | CXXTokenTypeEOF))
	{
		CXX_DEBUG_LEAVE_TEXT("Failed to parse if/for/while/switch up to parenthesis");
		return FALSE;
	}
	
	if(g_cxx.pToken->eType & (CXXTokenTypeEOF | CXXTokenTypeSemicolon))
	{
		CXX_DEBUG_LEAVE_TEXT("Found EOF/semicolon while parsing if/for/while/switch");
		return TRUE;
	}
	
	if(g_cxx.pToken->eType == CXXTokenTypeParenthesisChain)
	{
		// FIXME: Extract variable declarations from the parenthesis chain!

		CXX_DEBUG_LEAVE_TEXT("Found if/for/while/switch parenthesis chain");
		return TRUE;
	}

	// must be opening bracket: parse it here.

	boolean bRet = cxxParserParseBlock(TRUE);

	CXX_DEBUG_LEAVE();

	return bRet;
}

rescanReason cxxParserMain(const unsigned int passCount)
{
	if(g_bFirstRun)
	{
		cxxTokenAPIInit();

		g_cxx.pTokenChain = cxxTokenChainCreate();

		cxxScopeInit();
		
		g_bFirstRun = FALSE;
	} else {
		// Only clean state
		cxxScopeClear();
		cxxTokenAPINewFile();
		cxxParserNewStatement();
	}

	kindOption * kind_for_define = cxxTagGetKindOptions() + CXXTagKindMACRO;
	kindOption * kind_for_header = cxxTagGetKindOptions() + CXXTagKindINCLUDE;
	int role_for_macro_undef = CR_MACRO_UNDEF;
	int role_for_header_system = CR_HEADER_SYSTEM;
	int role_for_header_local = CR_HEADER_LOCAL;

	Assert(passCount < 3);

	cppInit(
			(boolean) (passCount > 1),
			FALSE,
			TRUE, // raw literals
			FALSE,
			kind_for_define,
			role_for_macro_undef,
			kind_for_header,
			role_for_header_system,
			role_for_header_local
		);

	g_cxx.iChar = ' ';

	boolean bRet = cxxParserParseBlock(FALSE);

	cppTerminate ();

	cxxTokenChainClear(g_cxx.pTokenChain);
	if(g_cxx.pTemplateTokenChain)
		cxxTokenChainClear(g_cxx.pTemplateTokenChain);
	
	if(!bRet && (passCount == 1))
	{
		CXX_DEBUG_PRINT("Processing failed: trying to rescan");
		return RESCAN_FAILED;
	}
	
	return RESCAN_NONE;
}

void cxxCppParserInitialize(const langType language)
{
	CXX_DEBUG_INIT();

	CXX_DEBUG_PRINT("Parser initialize for language C++");
	if(g_bFirstRun)
		memset(&g_cxx,0,sizeof(CXXParserState));

	g_cxx.eLanguage = language;
	g_cxx.eCPPLanguage = language;
	g_cxx.eCLanguage = -1;
	cxxBuildKeywordHash(language,TRUE);
}

void cxxCParserInitialize(const langType language)
{
	CXX_DEBUG_INIT();

	CXX_DEBUG_PRINT("Parser initialize for language C");
	if(g_bFirstRun)
		memset(&g_cxx,0,sizeof(CXXParserState));

	g_cxx.eLanguage = language;
	g_cxx.eCLanguage = language;
	g_cxx.eCPPLanguage = -1;
	cxxBuildKeywordHash(language,FALSE);
}

void cxxParserCleanup()
{
	if(g_bFirstRun)
		return; // didn't run at all

	if(g_cxx.pTokenChain)
		cxxTokenChainDestroy(g_cxx.pTokenChain);
	if(g_cxx.pTemplateTokenChain)
		cxxTokenChainDestroy(g_cxx.pTemplateTokenChain);

	cxxScopeDone();

	cxxTokenAPIDone();
}