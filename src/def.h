#include <stdio.h>

#define DEBUG 1
#define PRINT_MESSAGE(msg) printf("%s\n", msg);
#define PRINT_ERROR(msg) fprintf(stderr, "%s\n", msg);
#define DEBUG_MESSAGE(msg, ...) \
            do { if (DEBUG) fprintf(stderr, msg, __VA_ARGS__); } while (0)

