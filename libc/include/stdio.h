/* Genix minimal stdio.h */
#ifndef _STDIO_H
#define _STDIO_H

#ifndef NULL
#define NULL ((void *)0)
#endif

#define EOF (-1)

typedef struct _FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int    fileno(FILE *f);
FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *f);
char  *fgets(char *s, int size, FILE *f);
int    feof(FILE *f);
int    ferror(FILE *f);
int    fflush(FILE *f);
int    fputc(int c, FILE *f);
int    fgetc(FILE *f);
int    puts(const char *s);
int    printf(const char *fmt, ...);
int    fprintf(FILE *f, const char *fmt, ...);
int    sprintf(char *buf, const char *fmt, ...);
int    snprintf(char *buf, unsigned int size, const char *fmt, ...);

#endif
