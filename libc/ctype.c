/*
 * Minimal ctype for Genix user programs.
 */

int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}

int isalpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

int isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

int islower(int c)
{
    return c >= 'a' && c <= 'z';
}

int isprint(int c)
{
    return c >= ' ' && c <= '~';
}

int iscntrl(int c)
{
    return (c >= 0 && c < ' ') || c == 127;
}

int toupper(int c)
{
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 'A';
    return c;
}

int tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}
