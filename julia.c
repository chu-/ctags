/*
*   $Id: julia.c 571 2007-06-24 23:32:14Z elliotth $
*
*   Copyright (c) 2000-2001, Thaddeus Covert <sahuagin@mediaone.net>
*   Copyright (c) 2002 Matthias Veit <matthias_veit@yahoo.de>
*   Copyright (c) 2004 Elliott Hughes <enh@acm.org>
*   Copyright (c) 2013 Shibin Chu<io.epiphany@gmail.com>
*
*   This source code is released for free distribution under the terms of the
*   GNU General Public License.
*
*   This module contains functions for generating tags for Julia language
*   files.
*/

/*
*   INCLUDE FILES
*/
#include "general.h"  /* must always come first */

#include <string.h>

#include "entry.h"
#include "parse.h"
#include "read.h"
#include "vstring.h"

/*
*   DATA DECLARATIONS
*/
typedef enum {
	K_UNDEFINED = -1, K_METHOD, K_CLASS, K_MODULE, K_SINGLETON, K_DESCRIBE, K_CONTEXT, K_MACRO, K_TYPE, K_IMMU
} juliaKind;

/*
*   DATA DEFINITIONS
*/
static kindOption JuliaKinds [] = {
	{ TRUE, 'f', "function", "functions" },
	{ TRUE, 'c', "class", "classes" },
	{ TRUE, 'm', "module", "modules" },
	{ TRUE, 'F', "singleton method", "singleton methods" },
	{ TRUE, 'd', "describe", "describes" },
	{ TRUE, 'C', "context", "contexts" },
	{ TRUE, 'M', "macro", "macros" },
	{ TRUE, 't', "type", "types" },
	{ TRUE, 'i', "immutable", "immutables" }
};

static stringList* nesting = 0;

/*
*   FUNCTION DEFINITIONS
*/

/*
* Returns a string describing the scope in 'list'.
* We record the current scope as a list of entered scopes.
* Scopes corresponding to 'if' statements and the like are
* represented by empty strings. Scopes corresponding to
* modules and classes are represented by the name of the
* module or class.
*/
static vString* stringListToScope (const stringList* list)
{
	unsigned int i;
	unsigned int chunks_output = 0;
	vString* result = vStringNew ();
	const unsigned int max = stringListCount (list);
	for (i = 0; i < max; ++i)
	{
	    vString* chunk = stringListItem (list, i);
	    if (vStringLength (chunk) > 0)
	    {
	        vStringCatS (result, (chunks_output++ > 0) ? "." : "");
	        vStringCatS (result, vStringValue (chunk));
	    }
	}
	return result;
}

/*
* Attempts to advance 's' past 'literal'.
* Returns TRUE if it did, FALSE (and leaves 's' where
* it was) otherwise.
*/
static boolean canMatch (const unsigned char** s, const char* literal)
{
	const int literal_length = strlen (literal);
	const unsigned char next_char = *(*s + literal_length);
	if (strncmp ((const char*) *s, literal, literal_length) != 0)
	{
	    return FALSE;
	}
	/* Additionally check that we're at the end of a token. */
	if ( ! (next_char == 0 || isspace (next_char) || next_char == '('))
	{
	    return FALSE;
	}
	*s += literal_length;
	return TRUE;
}

/*
* Attempts to advance 'cp' past a Julia operator method name. Returns
* TRUE if successful (and copies the name into 'name'), FALSE otherwise.
*/
static boolean parseJuliaOperator (vString* name, const unsigned char** cp)
{
	static const char* julia_OPERATORS[] = {
	    "[]", "[]=",
	    "**",
	    "!", "~", "+@", "-@",
	    "*", "/", "%",
	    "+", "-",
	    ">>", "<<",
	    "&",
	    "^", "|",
	    "<=", "<", ">", ">=",
	    "<=>", "==", "===", "!=", "=~", "!~",
	    "`",
	    0
	};
	int i;
	for (i = 0; julia_OPERATORS[i] != 0; ++i)
	{
	    if (canMatch (cp, julia_OPERATORS[i]))
	    {
	        vStringCatS (name, julia_OPERATORS[i]);
	        return TRUE;
	    }
	}
	return FALSE;
}

/*
* Emits a tag for the given 'name' of kind 'kind' at the current nesting.
*/
static void emitJuliaTag (vString* name, juliaKind kind)
{
	tagEntryInfo tag;
	vString* scope;

	vStringTerminate (name);
	scope = stringListToScope (nesting);

	initTagEntry (&tag, vStringValue (name));
	if (vStringLength (scope) > 0) {
	    tag.extensionFields.scope [0] = "class";
	    tag.extensionFields.scope [1] = vStringValue (scope);
	}
	tag.kindName = JuliaKinds [kind].name;
	tag.kind = JuliaKinds [kind].letter;
	makeTagEntry (&tag);

	stringListAdd (nesting, vStringNewCopy (name));

	vStringClear (name);
	vStringDelete (scope);
}

/* Tests whether 'ch' is a character in 'list'. */
static boolean charIsIn (char ch, const char* list)
{
	return (strchr (list, ch) != 0);
}

/* Advances 'cp' over leading whitespace. */
static void skipWhitespace (const unsigned char** cp)
{
	while (isspace (**cp))
	{
	    ++*cp;
	}
}

/*
* Copies the characters forming an identifier from *cp into
* name, leaving *cp pointing to the character after the identifier.
*/
static juliaKind parseIdentifier (
		const unsigned char** cp, vString* name, juliaKind kind)
{
	/* Method names are slightly different to class and variable names.
	 * A method name may optionally end with a question mark, exclamation
	 * point or equals sign. These are all part of the name.
	 * A method name may also contain a period if it's a singleton method.
	 */
	const char* also_ok;
    if (kind == K_METHOD)
	{
		also_ok = "_.?!=";
	}
	else if (kind == K_DESCRIBE || kind == K_CONTEXT)
	{
		also_ok = " ,\".#_?!='/-";
	}
	else 
	{
		also_ok = "_";
	}

	skipWhitespace (cp);

	/* Check for an anonymous (singleton) class such as "class << HTTP". */
	if (kind == K_CLASS && **cp == '<' && *(*cp + 1) == '<')
	{
		return K_UNDEFINED;
	}

	/* Check for operators such as "def []=(key, val)". */
	if (kind == K_METHOD || kind == K_SINGLETON)
	{
		if (parseJuliaOperator (name, cp))
		{
			return kind;
		}
	}

	/* Copy the identifier into 'name'. */
	while (**cp != 0 && (isalnum (**cp) || charIsIn (**cp, also_ok)))
	{
		char last_char = **cp;

		vStringPut (name, last_char);
		++*cp;

		if (kind == K_METHOD)
		{
			/* Recognize singleton methods. */
			if (last_char == '.')
			{
				vStringTerminate (name);
				vStringClear (name);
				return parseIdentifier (cp, name, K_SINGLETON);
			}

			/* Recognize characters which mark the end of a method name. */
			if (charIsIn (last_char, "?!="))
			{
				break;
			}
		}
	}
	return kind;
}

static void readAndEmitTag (const unsigned char** cp, juliaKind expected_kind)
{
	if (isspace (**cp))
	{
		vString *name = vStringNew ();
		juliaKind actual_kind = parseIdentifier (cp, name, expected_kind);

		if (actual_kind == K_UNDEFINED || vStringLength (name) == 0)
		{
			/*
			* What kind of tags should we create for code like this?
			*
			*    %w(self.clfloor clfloor).each do |name|
			*        module_eval <<-"end;"
			*            def #{name}(x, y=1)
			*                q, r = x.divmod(y)
			*                q = q.to_i
			*                return q, r
			*            end
			*        end;
			*    end
			*
			* Or this?
			*
			*    class << HTTP
			*
			* For now, we don't create any.
			*/
		}
		else
		{
			emitJuliaTag (name, actual_kind);
		}
		vStringDelete (name);
	}
}

static void enterUnnamedScope (void)
{
	stringListAdd (nesting, vStringNewInit (""));
}

static void findJuliaTags (void)
{
	const unsigned char *line;
	boolean inMultiLineComment = FALSE;

	nesting = stringListNew ();

	/* FIXME: this whole scheme is wrong, because Julia isn't line-based.
	* You could perfectly well write:
	*
	*  def
	*  method
	*   puts("hello")
	*  end
	*
	* if you wished, and this function would fail to recognize anything.
	*/
	while ((line = fileReadLine ()) != NULL)
	{
		const unsigned char *cp = line;

		if (canMatch (&cp, "=begin"))
		{
			inMultiLineComment = TRUE;
			continue;
		}
		if (canMatch (&cp, "=end"))
		{
			inMultiLineComment = FALSE;
			continue;
		}

		skipWhitespace (&cp);

		/* Avoid mistakenly starting a scope for modifiers such as
		*
		*   return if <exp>
		*
		* FIXME: this is fooled by code such as
		*
		*   result = if <exp>
		*               <a>
		*            else
		*               <b>
		*            end
		*
		* FIXME: we're also fooled if someone does something heinous such as
		*
		*   puts("hello") \
		*       unless <exp>
		*/
		if (
                canMatch (&cp, "case")   || 
                canMatch (&cp, "for")    ||
                canMatch (&cp, "if")     || 
                canMatch (&cp, "unless") ||
                canMatch (&cp, "quote") ||
                canMatch (&cp, "let") ||
                canMatch (&cp, "begin") ||
                canMatch (&cp, "catch") ||
                canMatch (&cp, "while")
           )
		{
			enterUnnamedScope ();
		}

		/*
		* "module M", "class C" and "function m" should only be at the beginning
		* of a line.
		*/
		if (canMatch (&cp, "function"))
		{
			readAndEmitTag (&cp, K_METHOD);
		}
        else if (canMatch (&cp, "class"))
		{
			readAndEmitTag (&cp, K_CLASS);
		}
		else if (canMatch (&cp, "module"))
		{
			readAndEmitTag (&cp, K_MODULE);
		}
		else if (canMatch (&cp, "describe"))
		{
			readAndEmitTag (&cp, K_DESCRIBE);
		}
		else if (canMatch (&cp, "context"))
		{
			readAndEmitTag (&cp, K_CONTEXT);
		}
		else if (canMatch (&cp, "macro"))
		{
			readAndEmitTag (&cp, K_MACRO);
		}
		else if (canMatch (&cp, "type"))
		{
			readAndEmitTag (&cp, K_TYPE);
		}
		else if (canMatch (&cp, "immutable"))
		{
			readAndEmitTag (&cp, K_IMMU);
		}

		while (*cp != '\0')
		{
			/* FIXME: we don't cope with here documents,
			* or regular expression literals, or ... you get the idea.
			* Hopefully, the restriction above that insists on seeing
			* definitions at the starts of lines should keep us out of
			* mischief.
			*/
			if (inMultiLineComment || isspace (*cp))
			{
				++cp;
			}
			else if (*cp == '#')
			{
				/* FIXME: this is wrong, but there *probably* won't be a
				* definition after an interpolated string (where # doesn't
				* mean 'comment').
				*/
				break;
			}
			else if (canMatch (&cp, "begin") || canMatch (&cp, "do"))
			{
				enterUnnamedScope ();
			}
			else if (canMatch (&cp, "end") && stringListCount (nesting) > 0)
			{
				/* Leave the most recent scope. */
				vStringDelete (stringListLast (nesting));
				stringListRemoveLast (nesting);
			}
			else if (*cp == '"')
			{
				/* Skip string literals.
				 * FIXME: should cope with escapes and interpolation.
				 */
				do {
					++cp;
				} while (*cp != 0 && *cp != '"');
			}
			else if (*cp != '\0')
			{
				do
					++cp;
				while (isalnum (*cp) || *cp == '_');
			}
		}
	}
	stringListDelete (nesting);
}

extern parserDefinition* JuliaParser (void)
{
	static const char *const extensions [] = { "jl", "julia", NULL };
	parserDefinition* def = parserNew ("Julia");
	def->kinds      = JuliaKinds;
	def->kindCount  = KIND_COUNT (JuliaKinds);
	def->extensions = extensions;
	def->parser     = findJuliaTags;
	return def;
}

/* vi:set tabstop=4 shiftwidth=4: */

