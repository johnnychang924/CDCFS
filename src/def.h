#include <iostream>

#ifndef DEF_H
#define DEF_H

#define BACKEND "/mnt/btrfs"
#define BLOCK_SIZE 4096

// don't change it!
#define MAX_INODE_NUM 4096


#ifdef DEBUG
#define DEBUG_MESSAGE(msg) std::cout << msg << std::endl
#else
#define DEBUG_MESSAGE(msg)
#endif

#define PRINT_WARNING(msg) std::cout << msg << std::endl

#endif /* DEF_H */