#include <iostream>

#ifndef DEF_H
#define DEF_H

#define BACKEND "/mnt/data/桌面/FUSECDC/CDCFS/bak"
#define BLOCK_SIZE 4096
#define MAX_GROUP_SIZE 4096

// don't change it!
#define MAX_INODE_NUM 1048576

// type define
#define INUM_TYPE uint32_t
#define FP_TYPE std::string
#define PATH_TYPE std::string

#ifdef DEBUG
#define DEBUG_MESSAGE(msg) std::cout << msg << std::endl
#else
#define DEBUG_MESSAGE(msg)
#endif

#define PRINT_MESSAGE(msg) std::cout << msg << std::endl
#define PRINT_WARNING(msg) std::cerr << msg << std::endl

#endif /* DEF_H */