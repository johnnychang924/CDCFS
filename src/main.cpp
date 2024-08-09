#define FUSE_USE_VERSION 30

#include "file.h"
#include "dir.h"

void cdcfs_leave(void *param){
    PRINT_MESSAGE("\n----------------------------------------leaving CDCFS !!----------------------------------------");
    PRINT_MESSAGE("total write size:" << (float)total_write_size / 1000000000 << "GB");
    PRINT_MESSAGE("total dedup rate:" << (float)total_dedup_size / total_write_size * 100 << "%");
}

static struct fuse_operations cdcfs_oper = {
    .getattr        = cdcfs_getattr,
    .readlink       = cdcfs_readlink,
    .mkdir          = cdcfs_mkdir,
    //.unlink         = cdcfs_unlink,
    .rmdir          = cdcfs_rmdir,
    .symlink        = cdcfs_symlink,
    .link           = cdcfs_link,
    .truncate       = cdcfs_truncate,
    .utime          = cdcfs_utime,
    .open           = cdcfs_open,
    .read           = cdcfs_read,
    .write          = cdcfs_write,
    .release        = cdcfs_release,
    .opendir        = cdcfs_opendir,
    .readdir        = cdcfs_readdir,
    .releasedir     = cdcfs_releasedir,
    .destroy        = cdcfs_leave,
    .create         = cdcfs_create,
    .ftruncate      = cdcfs_ftruncate,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &cdcfs_oper, NULL);
}