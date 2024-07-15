#include <stdio.h>

#ifndef DEF_H
#define DEF_H

#define backend "/mnt/btrfs"

#ifdef DEBUG
#define DEBUG_MESSAGE(fmt, ...) \
    do { fprintf(stderr, fmt, __VA_ARGS__); } while (0)
#else
#define DEBUG_MESSAGE(fmt, ...) \
    do { } while (0)
#endif

#endif /* DEF_H */