#include <stddef.h>
#include <stdio.h>
struct TraceFileAlignment { char tag; FILE file; };
unsigned long RustAbiTraceLayout[] = {
    sizeof(FILE), offsetof(struct TraceFileAlignment, file), 2 * sizeof(FILE),
    sizeof(FILE *), sizeof(int), sizeof(char *), offsetof(FILE, f50)
};
