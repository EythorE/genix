/*
 * Genix minimal regex engine
 *
 * Supports: literal chars, . ^ $ * [abc] [^abc] [a-z] \ escaping
 * Recursive backtracking matcher — no dynamic allocation.
 * Suitable for 68000 with 64 KB RAM.
 */

#include <regex.h>
#include <string.h>

/* Forward declarations */
static int match_here(const char *pat, const char *text, const char **end);
static int match_star(char c, int is_class, const char *class_start,
                      const char *pat, const char *text, const char **end);

/* Check if character c matches a character class starting at pat.
 * pat points to the first char after '[' (or after '[^').
 * class_end is set to point past the closing ']'.
 * Returns 1 if c is in the class. */
static int match_class(char c, const char *pat, const char **class_end)
{
    int negate = 0;
    const char *p = pat;
    int found = 0;

    if (*p == '^') {
        negate = 1;
        p++;
    }

    /* First char after '[' or '[^' can be ']' literally */
    while (*p && *p != ']') {
        /* Range: a-z */
        if (p[1] == '-' && p[2] && p[2] != ']') {
            if ((unsigned char)c >= (unsigned char)p[0] &&
                (unsigned char)c <= (unsigned char)p[2])
                found = 1;
            p += 3;
        } else {
            if (c == *p)
                found = 1;
            p++;
        }
    }

    if (*p == ']')
        p++;
    if (class_end)
        *class_end = p;

    return negate ? !found : found;
}

/* Advance past one pattern element (literal, escape, or class).
 * Returns pointer to the next pattern element. */
static const char *pat_next(const char *pat)
{
    if (*pat == '\\' && pat[1])
        return pat + 2;
    if (*pat == '[') {
        const char *end;
        match_class(0, pat + 1, &end);
        return end;
    }
    return pat + 1;
}

/* Does one pattern element match character c?
 * pat points to the element (literal, '.', escape, or '['). */
static int match_one(const char *pat, char c)
{
    if (c == '\0')
        return 0;
    if (*pat == '.')
        return 1;
    if (*pat == '\\' && pat[1])
        return c == pat[1];
    if (*pat == '[')
        return match_class(c, pat + 1, NULL);
    return c == *pat;
}

/* Match pattern at current text position.
 * Sets *end to one past the last matched character on success.
 * Returns 1 on match, 0 on no match. */
static int match_here(const char *pat, const char *text, const char **end)
{
    /* End of pattern — match */
    if (*pat == '\0') {
        *end = text;
        return 1;
    }

    /* '$' at end of pattern matches end of string */
    if (pat[0] == '$' && pat[1] == '\0') {
        if (*text == '\0') {
            *end = text;
            return 1;
        }
        return 0;
    }

    /* Check for '*' after this element */
    {
        const char *next = pat_next(pat);
        if (*next == '*') {
            const char *after_star = next + 1;
            if (*pat == '[') {
                return match_star(0, 1, pat, after_star, text, end);
            } else {
                char c = (*pat == '\\' && pat[1]) ? pat[1] :
                         (*pat == '.' ? '.' : *pat);
                int is_dot = (*pat == '.');
                /* For match_star, we pass the char to match.
                 * If '.', match_star handles any char. */
                (void)is_dot;
                return match_star(c, 0, pat, after_star, text, end);
            }
        }
    }

    /* Regular single-character match */
    if (match_one(pat, *text))
        return match_here(pat_next(pat), text + 1, end);

    return 0;
}

/* Match c* (or .*  or [class]*) followed by rest of pattern.
 * Greedy: try longest match first, then backtrack. */
static int match_star(char c, int is_class, const char *class_pat,
                      const char *pat, const char *text, const char **end)
{
    const char *t = text;

    /* Advance as far as possible */
    if (is_class) {
        while (*t && match_class(*t, class_pat + 1, NULL))
            t++;
    } else if (c == '.') {
        while (*t)
            t++;
    } else {
        while (*t == c)
            t++;
    }

    /* Backtrack: try matching rest from each position */
    while (t >= text) {
        if (match_here(pat, t, end))
            return 1;
        t--;
    }
    return 0;
}

/* --- Public API --- */

int regcomp(regex_t *preg, const char *pattern)
{
    int len;

    if (!preg || !pattern)
        return -1;

    len = strlen(pattern);
    if (len >= RE_MAXPAT) {
        preg->valid = 0;
        return -1;
    }

    /* Basic syntax validation */
    {
        const char *p = pattern;
        while (*p) {
            if (*p == '\\') {
                if (!p[1]) {
                    preg->valid = 0;
                    return -1;  /* trailing backslash */
                }
                p += 2;
            } else if (*p == '[') {
                p++;
                if (*p == '^') p++;
                if (*p == ']') p++;  /* literal ] as first char */
                while (*p && *p != ']') p++;
                if (*p != ']') {
                    preg->valid = 0;
                    return -1;  /* unclosed [ */
                }
                p++;
            } else {
                p++;
            }
        }
    }

    strcpy(preg->pattern, pattern);
    preg->valid = 1;
    return 0;
}

int regexec(const regex_t *preg, const char *string,
            const char **match_start, const char **match_end)
{
    const char *pat;
    const char *text;
    const char *end;

    if (!preg || !preg->valid || !string)
        return -1;

    pat = preg->pattern;
    text = string;

    /* '^' anchor: only try at start */
    if (*pat == '^') {
        if (match_here(pat + 1, text, &end)) {
            if (match_start) *match_start = text;
            if (match_end)   *match_end = end;
            return 0;
        }
        return -1;
    }

    /* Unanchored: try match at each position */
    do {
        if (match_here(pat, text, &end)) {
            if (match_start) *match_start = text;
            if (match_end)   *match_end = end;
            return 0;
        }
    } while (*text++);

    return -1;
}

void regfree(regex_t *preg)
{
    if (preg)
        preg->valid = 0;
}
