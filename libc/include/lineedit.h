/* Genix lineedit.h — single-line editor with history */
#ifndef _LINEEDIT_H
#define _LINEEDIT_H

#define LE_LINE_MAX  256

void le_init(int cols);
int  le_readline(int fd, char *buf, int bufsz);

#endif
