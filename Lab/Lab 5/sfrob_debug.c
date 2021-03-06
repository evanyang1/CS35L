/*
 *  sfrob
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define SPACE       ' '
#define INIT_SIZE   20

char decrypt(const char c);
int  frobcmp(char const* a, char const* b);
int  cmpWrapper(const void* a, const void* b);
void freeWrapper(void* ptr, int* num);
void checkMemErr(void* ptr, int* num);
void checkIOErr(FILE* p);
void reportErr(const char* msg);

int main(void)
{
    /* Declare Variables, a pain in C */
    int (* cmp) (const void*, const void*);
    char** input, ** input2, * linebuf, * linebuf2, curChar;
    size_t maxLineNum, lineNum, lineSize, bufferSize, strSize, i, j;
    int isEOF, isSpace, allocation;
    
    /* Initialize variables */
    bufferSize = INIT_SIZE, maxLineNum = INIT_SIZE;
    lineNum = 0, lineSize = 0, isEOF = feof(stdin), strSize = 0, allocation = 0;
    
    /* Set frobcmp pointer */
    cmp = &cmpWrapper;
    
    /* Setup Initial Buffer */
    linebuf = (char*) malloc(sizeof(char) * bufferSize);
    checkMemErr(linebuf, &allocation);
    input = (char**) malloc(sizeof(char*) * maxLineNum);
    checkMemErr(input, &allocation);
    
    /*                                                      *
     *  To prevent getting a char with value EOF,           *
     *  We use feof(stdin) as the condition for the loop.   *
     */
    while ( ! isEOF)
    {
        curChar = getchar();
        isSpace = curChar == SPACE;
        isEOF = feof(stdin);
        if (lineSize == 0 && isSpace)
            continue;       /* Skip consecutive spaces */
        
        /* Resize buffer */
        if (lineSize == bufferSize)
        {
            bufferSize *= 2;
            linebuf2 = (char*) realloc(linebuf, sizeof(char) * bufferSize);
            checkMemErr(linebuf2, NULL);
            linebuf = linebuf2;
        }
        
        /* Store the current character */
        if ( ! isEOF)
        {
            strSize++;
            linebuf[lineSize++] = curChar;
            if ( ! isSpace)
                continue;
        }
        else
        {
            checkIOErr(stdin);    /* Check IO Error if getchar returns EOF */
            if ( ! strSize)
            {
                freeWrapper(input, &allocation);
                freeWrapper(linebuf, &allocation);
                if (allocation)
                    reportErr("Memory Leak");   /* Check Memory Leak */
                return 0;         /* An Empty file or a file with only spaces */
            }
            /* Append a space if there is none */
            if ( ! lineSize && linebuf != NULL)
                freeWrapper(linebuf, &allocation);
            else if (lineSize && linebuf[lineSize-1] != SPACE)
                linebuf[lineSize++] = SPACE;
        }
        
        /* Reached a new line */
        input[lineNum++] = linebuf;
        linebuf = NULL;
        
        if ( ! isEOF)
        {
            /* Resize input */
            if (lineNum == maxLineNum)
            {
                maxLineNum *= 2;
                input2 = (char**) realloc(input, sizeof(char*) * maxLineNum);
                checkMemErr(input2, NULL);
                input = input2;
            }
            lineSize = 0;
            
            /* Allocate storage for a new line */
            bufferSize = INIT_SIZE;
            linebuf = (char*) malloc(sizeof(char) * bufferSize);
            checkMemErr(linebuf, &allocation);
        }
    }
    
    /* Sort the input */
    qsort(input, lineNum, sizeof(char*), cmp);
    
    /* Output results */
    for (i = 0; i < lineNum; i++)
    {
        for (j = 0, curChar = input[i][0]; curChar != SPACE; curChar = input[i][++j])
        {
            putchar(curChar);
            checkIOErr(stdout);
        }
        putchar(curChar);
        checkIOErr(stdout);
    }
    
    /* Free input array */
    for (i = 0; i < lineNum; i++)
        freeWrapper(input[i], &allocation);
    freeWrapper(input, &allocation);
    
    if (allocation)
        reportErr("Memory Leak");       /* Check Memory Leak */
    return 0;
}


/* Report error */
inline
void reportErr(const char* msg)
{
    fprintf(stderr, "%s Error: %d\n", msg, errno);
    exit(1);
}

/* Check IO Error */
inline
void checkIOErr(FILE* p)
{
    if (ferror(p))      reportErr("IO");
}

/* Check if malloc / realloc allocates memory successfully */
inline
void checkMemErr(void* ptr, int* num)
{
    if (num != NULL)    (*num)++;
    if (ptr == NULL)    reportErr("Memory Allocation");
}

/* Wrapper for free that helps check memory leak */
inline
void freeWrapper(void* ptr, int* num)
{
    if (num == NULL || ptr == NULL)
        reportErr("Bad Arguments for free wrapper");
    (*num)--;
    free(ptr);
}


/*  Decrypt each character */
inline
char decrypt(const char c)
{
    return c ^ 42;      /* 00101010 is 0x2a, 42 */
}

/* Wrapper function */
inline
int cmpWrapper(const void* a, const void* b)
{
    return frobcmp(*((const char**) a), *((const char**) b));
}


/* Compare two frobnicated characters */
int frobcmp(char const* a, char const* b)
{
    for ( ; *a == *b; a++, b++)
        if (*a == SPACE)
            return 0;
    return ((decrypt(*a) < (decrypt(*b)) ? -1 : 1));
    
}
