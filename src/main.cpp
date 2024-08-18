#define FUSE_USE_VERSION 30

#include <filesystem>
#include "file.h"
#include "dir.h"

void cdcfs_leave(void *param){
    PRINT_MESSAGE("\n----------------------------------------leaving CDCFS !!!----------------------------------------");
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
    //.truncate       = cdcfs_truncate,
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
    // remove every file in backend directory.
    bool show_confirm = false;
    char replay;
    for (const auto& entry : std::filesystem::directory_iterator(BACKEND)){
        if (!show_confirm){
            PRINT_MESSAGE("WARNING: BACKEND directory is not empty, all files in it will be removed!![y|n]");
            std::cin >> replay;
            show_confirm = true;
            if (replay == 'n') return 0;
        }
        std::filesystem::remove_all(entry.path());
    }
    // init CDCFS data structure
    PRINT_MESSAGE("----------------------------------------entering CDCFS !!----------------------------------------");
    for (INUM_TYPE iNum = 0; iNum < MAX_INODE_NUM - 1; ++iNum) {
        free_iNum.insert(iNum);
    }
    for(FILE_HANDLER_INDEX_TYPE file_handler = 0; file_handler < MAX_FILE_HANDLER - 1; ++file_handler){
        free_file_handler.insert(file_handler);
    }
    // init fastcdc engine
    cdc = fastcdc_init(0, 4096, 16384);
    //cdc = fastcdc_init(512, 512, 512);
    ctx = &cdc;
    // start CDCFS
    return fuse_main(argc, argv, &cdcfs_oper, NULL);
}