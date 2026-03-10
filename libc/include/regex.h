/* Genix minimal regex.h — simple pattern matching */
#ifndef _REGEX_H
#define _REGEX_H

/* Compiled regex — opaque, max 128 bytes */
#define RE_MAXPAT 128

typedef struct {
    char pattern[RE_MAXPAT];
    int valid;
} regex_t;

/* Compile a regex pattern. Returns 0 on success, -1 on error. */
int regcomp(regex_t *preg, const char *pattern);

/* Match a compiled regex against a string.
 * Returns 0 if match found, -1 if no match.
 * If match_start/match_end are non-NULL, they're set to the match position. */
int regexec(const regex_t *preg, const char *string,
            const char **match_start, const char **match_end);

/* Free a compiled regex (no-op for our implementation). */
void regfree(regex_t *preg);

#endif
