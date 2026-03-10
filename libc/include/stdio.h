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
int    sscanf(const char *str, const char *fmt, ...);

int          fputs(const char *s, FILE *f);
unsigned int fread(void *ptr, unsigned int size, unsigned int nmemb, FILE *f);
unsigned int fwrite(const void *ptr, unsigned int size, unsigned int nmemb, FILE *f);
int          ungetc(int c, FILE *f);

#define putchar(c) fputc((c), stdout)
#define getchar()  fgetc(stdin)
#define putc(c, f) fputc((c), (f))
#define getc(f)    fgetc((f))

#endif
