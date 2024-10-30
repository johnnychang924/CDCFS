#include "def.h"

/*
* The storage backend of CDCFS. Currently using zonefs
*/
class CDCFS_BACKEND{
    CDCFS_BACKEND();
    ~CDCFS_BACKEND();
    int max_open_zone;
};