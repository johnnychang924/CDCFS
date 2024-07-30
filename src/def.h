#include <iostream>

#ifndef DEF_H
#define DEF_H

#define BACKEND "/mnt/data/FUSECDC/CDCFS/bak"
#define BLOCK_SIZE 4096
#define MAX_GROUP_SIZE 4096

// don't change it!
#define MAX_INODE_NUM 256

// type define
#define INUM_TYPE uint8_t
#define FP_TYPE std::string
#define PATH_TYPE std::string

#ifdef DEBUG
#define DEBUG_MESSAGE(msg) std::cout << msg << std::endl
#else
#define DEBUG_MESSAGE(msg)
#endif

#define PRINT_WARNING(msg) std::cout << msg << std::endl

#endif /* DEF_H */