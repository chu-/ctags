/***************************************************************************
 *  * julia.c
 *   * Character-based parser for Julia definitions
 *    **************************************************************************/
/* INCLUDE FILES */
#include "general.h"    /* always include first */

#include <string.h>     /* to declare strxxx() functions */
#include <ctype.h>      /* to define isxxx() macros */

#include "parse.h"      /* always include */
#include "read.h"       /* to define file fileReadLine() */

/* DATA DEFINITIONS */
typedef enum eJuliaKinds {
    K_DEFINE
} juliaKind;

static kindOption JuliaKinds [] = {
    { TRUE, 'f', "function", "functions" },
};

/* FUNCTION DEFINITIONS */

static void findJuliaTags (void)
{
    vString *name = vStringNew ();
    const unsigned char *line;

    while ((line = fileReadLine ()) != NULL)
    {
            /* Look for a line beginning with "function" followed by name */
            if (strncmp ((const char*) line, "function", (size_t) 8) == 0  &&
                                        isspace ((int) line [8]))
            {
                        const unsigned char *cp = line + 9;
                        while (isspace ((int) *cp))
                            ++cp;
                        while (isalnum ((int) *cp)  ||  *cp == '_')
                        {
                                        vStringPut (name, (int) *cp);
                                        ++cp;
                                    }
                        vStringTerminate (name);
                        makeSimpleTag (name, JuliaKinds, K_DEFINE);
                        vStringClear (name);
                    }
        }
    vStringDelete (name);
}

/* Create parser definition stucture */
extern parserDefinition* JuliaParser (void)
{
    static const char *const extensions [] = { "jl", NULL };
    parserDefinition* def = parserNew ("Julia");
    def->kinds      = JuliaKinds;
    def->kindCount  = KIND_COUNT (JuliaKinds);
    def->extensions = extensions;
    def->parser     = findJuliaTags;
    return def;
}
