#include <fuse.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_map>
#include <map>
#include <openssl/sha.h>
#include <string>
#include <set>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include "def.h"
#include "fastcdc.h"

PATH_TYPE iNum_to_path[MAX_INODE_NUM];
std::unordered_map<PATH_TYPE, INUM_TYPE> path_to_iNum;
std::set<INUM_TYPE> free_iNum;
std::unordered_map<FP_TYPE, group_addr *> fp_store;
std::set<FILE_HANDLER_INDEX_TYPE> free_file_handler;
file_handler_data file_handler[MAX_FILE_HANDLER];   // get iNum by file handler (faster than get by file path)
mapping_table_entry mapping_table[MAX_INODE_NUM];

std::shared_mutex create_file_mutex;    // the lock for create new file
std::shared_mutex fp_store_mutex;       // the lock for access fp_store
std::shared_mutex file_handler_mutex;   // the lock for allocate file handler and free file handler
std::shared_mutex status_record_mutex;  // the lock for recording file system status
std::shared_mutex chunker_mutex;        // the lock for access chunker

unsigned long total_write_size = 0;     // total size of writed file in this file system
unsigned long total_dedup_size = 0;     // total size of writed file in this file system after deduplication

fcdc_ctx cdc, *ctx;

inline INUM_TYPE get_inum(PATH_TYPE path_str){
    std::shared_lock<std::shared_mutex> shared_create_file_lock(create_file_mutex);     // make sure nobody is creating new file at the same time
    auto it = path_to_iNum.find(path_str);
    shared_create_file_lock.unlock();
    if (it != path_to_iNum.end()){
        return it->second;
    }
    else {
        std::unique_lock<std::shared_mutex> unique_create_file_lock(create_file_mutex); // lock for creating new file
        if (free_iNum.empty()){
            PRINT_WARNING("run out of iNum");
            return -1;
        }
        INUM_TYPE new_iNum = *free_iNum.begin();
        free_iNum.erase(free_iNum.begin());
        path_to_iNum[path_str] = new_iNum;
        iNum_to_path[new_iNum] = path_str;
        return new_iNum;
    }
}

inline PATH_TYPE get_path(INUM_TYPE iNum){
    std::shared_lock<std::shared_mutex> shared_create_file_lock(create_file_mutex);      // make sure nobody is creating new file at the same time
    return iNum_to_path[iNum];
}

inline FILE_HANDLER_INDEX_TYPE get_free_file_handler(){
    std::unique_lock<std::shared_mutex> unique_file_handler_lock(file_handler_mutex);   // lock for allocating new file handler
    if (free_file_handler.empty()){
        PRINT_WARNING("run out of file handlers");
        return -1;
    }
    FILE_HANDLER_INDEX_TYPE new_file_handler_index = *free_file_handler.begin();
    free_file_handler.erase(free_file_handler.begin());
    return new_file_handler_index;
}

inline void release_file_handler(FILE_HANDLER_INDEX_TYPE file_handler_index){
    std::unique_lock<std::shared_mutex> unique_file_handler_lock(file_handler_mutex);    // lock for freeing file handler
    free_file_handler.insert(file_handler_index);
}

inline void init_file_handler(const char *path, FILE_HANDLER_INDEX_TYPE file_handler_index, int real_file_handler, char mode){
    PATH_TYPE path_str(path);
    INUM_TYPE iNum = get_inum(path_str);
    file_handler[file_handler_index] = {
        .iNum = iNum,
        .fh = real_file_handler,
        .mode = mode,
    };
    if (mode == 'w'){
        file_handler[file_handler_index].write_buf = {
            .start_byte = 0,
            .byte_cnt = 0,
            .content = new char[MAX_GROUP_SIZE],
        };
    }
}

static int cdcfs_getattr(const char *path, struct stat *stbuf) {
    int res;
    char full_path[1024];
    PATH_TYPE path_str(path);
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("[getattr]" << path);

    res = lstat(full_path, stbuf);
    if (res == -1) {
        return -errno;
    }
    std::shared_lock<std::shared_mutex> shared_create_file_lock(create_file_mutex);     // make sure nobody is creating new file at the same time
    auto it = path_to_iNum.find(path_str);
    if (it != path_to_iNum.end()){
        INUM_TYPE iNum = it->second;
        stbuf->st_size = mapping_table[iNum].logical_size_for_host;
    }
    shared_create_file_lock.unlock();
    return 0;
}

static int cdcfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    int real_file_handler;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("[create]" << path);
    
    real_file_handler = creat(full_path, mode);
    if (real_file_handler == -1) return -errno;
    fi->fh = get_free_file_handler();
    if (fi->fh == (FILE_HANDLER_INDEX_TYPE)-1) return -errno;

    init_file_handler(path, fi->fh, real_file_handler, 'w');
    if (file_handler[fi->fh].fh == (FILE_HANDLER_INDEX_TYPE)-1) return -errno;
    else return 0;
}

static int cdcfs_open(const char *path, struct fuse_file_info *fi) {
    int real_file_handler;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("[open]" << path);

    real_file_handler = open(full_path, fi->flags);
    if (real_file_handler == -1) return -errno;

    fi->fh = get_free_file_handler();
    char mode = fi->flags & (O_WRONLY | O_RDWR) ? 'w' : 'r';
    DEBUG_MESSAGE("mode: " << mode);
    init_file_handler(path, fi->fh, real_file_handler, mode);
    if (file_handler[fi->fh].fh == (FILE_HANDLER_INDEX_TYPE)-1) return -errno;
    else return 0;
}

static int cdcfs_release(const char *path, struct fuse_file_info *fi) {
    int res;
    DEBUG_MESSAGE("[release]" << path);

    INUM_TYPE iNum = file_handler[fi->fh].iNum;
    buffer_entry *file_buffer = &file_handler[fi->fh].write_buf;

    // write back file buffer
    int write_back_ptr = 0;
    while (write_back_ptr < file_buffer->byte_cnt){
        DEBUG_MESSAGE("  start write back file buffer");
        // not implement content chunking yet, use fixed sized
        //int cut_pos = std::min(file_buffer->byte_cnt, (uint16_t)4096);
        //int cut_pos = file_buffer->byte_cnt;
        int cut_pos = cut((const uint8_t*)file_buffer->content + write_back_ptr, file_buffer->byte_cnt - write_back_ptr, ctx->mi, ctx->ma, ctx->ns,
                      ctx->mask_s, ctx->mask_l);

        #ifdef CAFTL
        cut_pos = std::min(file_buffer->byte_cnt, (uint16_t)BLOCK_SIZE); // use fixed chunking
        #endif
        DEBUG_MESSAGE("    cut pos: " << cut_pos << " actual_size_in_disk: " << mapping_table[iNum].actual_size_in_disk);

        std::unique_lock<std::shared_mutex> unique_status_record_lock(status_record_mutex);
        total_write_size += cut_pos;
        unique_status_record_lock.unlock();

        char cur_fp[SHA_DIGEST_LENGTH];
        SHA1((const unsigned char *)file_buffer->content + write_back_ptr, cut_pos, (unsigned char *)cur_fp);
        FP_TYPE new_fp(cur_fp, SHA_DIGEST_LENGTH);

        std::unique_lock<std::shared_mutex> shared_fp_store_lock(fp_store_mutex);
        auto fp_store_iter = fp_store.find(new_fp);
        shared_fp_store_lock.unlock();
        if (fp_store_iter != fp_store.end() && false){   // found
            DEBUG_MESSAGE("    found duplicate group!!");
            unique_status_record_lock.lock();
            total_dedup_size += cut_pos;
            unique_status_record_lock.unlock();
            mapping_table[iNum].group_pos.push_back(fp_store_iter->second);
            fp_store_iter->second->ref_times += 1;
            mapping_table[iNum].group_offset.push_back(file_buffer->start_byte + write_back_ptr);
            int blk_count = std::ceil((float)cut_pos / BLOCK_SIZE);
            for(int i = 0; i < blk_count; i++){
                mapping_table[iNum].group_idx.push_back(mapping_table[iNum].group_pos.size() - 1);
            }
        }
        else{                                   // not found
            group_addr *new_group_addr = new group_addr;
            new_group_addr->iNum = iNum;
            new_group_addr->ref_times = 1;
            new_group_addr->start_byte = mapping_table[iNum].actual_size_in_disk;
            new_group_addr->group_length = cut_pos;
            int res = pwrite(file_handler[fi->fh].fh, file_buffer->content + write_back_ptr, cut_pos, mapping_table[iNum].actual_size_in_disk);
            if (res == -1){
                PRINT_WARNING("write back to disk failed!!");
                delete new_group_addr;
                return -errno;
            }
            int blk_count = std::ceil((float)cut_pos / BLOCK_SIZE);
            mapping_table[iNum].actual_size_in_disk += cut_pos;
            mapping_table[iNum].group_pos.push_back(new_group_addr);
            mapping_table[iNum].group_offset.push_back(file_buffer->start_byte + write_back_ptr);
            for(int i = 0; i < blk_count; i++){
                mapping_table[iNum].group_idx.push_back(mapping_table[iNum].group_pos.size() - 1);
            }
            std::unique_lock<std::shared_mutex> unique_fp_store_lock(fp_store_mutex);
            fp_store[new_fp] = new_group_addr;
            unique_fp_store_lock.unlock();
        }

        write_back_ptr += cut_pos;
    }
    free(file_buffer->content);
    file_buffer->content = NULL;

    res = close(file_handler[fi->fh].fh);
    if (res == -1) {
        return -errno;
    }
    release_file_handler(fi->fh);
    return 0;
}

static int cdcfs_utime(const char *path, struct utimbuf *ubuf) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("[utime]" << path);

    res = utime(full_path, ubuf);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int res;
    DEBUG_MESSAGE("[read]" << path << " offset: " << offset << " size: " << size);

    INUM_TYPE iNum = file_handler[fi->fh].iNum;
    unsigned long cur_group_idx = (unsigned long)offset / BLOCK_SIZE < mapping_table[iNum].group_idx.size() ? 
        mapping_table[iNum].group_idx[offset / BLOCK_SIZE] : mapping_table[iNum].group_pos.size() - 1;
    while (true) {
        off_t cur_group_offset = mapping_table[iNum].group_offset[cur_group_idx];
        if (cur_group_offset > offset)
            cur_group_idx--;
        else if(cur_group_offset + mapping_table[iNum].group_pos[cur_group_idx]->group_length <= offset)
            cur_group_idx++;
        else break;
    }

    char *buf_ptr = buf;
    off_t cur_offset = offset;
    std::map<INUM_TYPE, uint64_t> fh_cache;     // cache other file handlers by inode number
    int total_read_byes = 0;
    size_t less_size = size;
    while(less_size > 0 && cur_group_idx < mapping_table[iNum].group_pos.size()) {
        int offset_in_group = cur_offset - mapping_table[iNum].group_offset[cur_group_idx];
        off_t read_start_offset = mapping_table[iNum].group_pos[cur_group_idx]->start_byte + offset_in_group;
        size_t read_size = mapping_table[iNum].group_pos[cur_group_idx]->group_length - offset_in_group;
        if (less_size < read_size) read_size = less_size;
        INUM_TYPE group_iNum = mapping_table[iNum].group_pos[cur_group_idx]->iNum;
        if (group_iNum == iNum){    // not being dedup group
            DEBUG_MESSAGE("  reading " << path << "(" << (int)group_iNum << ")" << " in group " << cur_group_idx << " from " << read_start_offset << " until " << read_size);
            res = pread(file_handler[fi->fh].fh, buf_ptr, read_size, read_start_offset);
        }
        else{                       // being dedup group
            if (fh_cache.find(group_iNum) == fh_cache.end()) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, iNum_to_path[group_iNum].c_str());
                fh_cache[group_iNum] = open(full_path, O_RDONLY | O_DIRECT);
            }
            DEBUG_MESSAGE("  reading " << iNum_to_path[group_iNum] << "(" << (int)group_iNum << ")" << " in duplicate group " << cur_group_idx << " from " << read_start_offset << " until " << read_size);
            res = pread(fh_cache[group_iNum], buf_ptr, read_size, read_start_offset);
        }
        if ((unsigned long)res < read_size) goto fail;
        buf_ptr += read_size;
        less_size -= read_size;
        cur_offset += read_size;
        cur_group_idx++;
        total_read_byes += read_size;
    }

    return total_read_byes;
fail:
    for (auto it = fh_cache.begin(); it!= fh_cache.end(); ++it) {
        close(it->second);
    }
    DEBUG_MESSAGE("    read fail: " << res);
    return -errno;
}

static int cdcfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    DEBUG_MESSAGE("[write]" << path << " offset: " << offset << " size: " << size);

    INUM_TYPE iNum = file_handler[fi->fh].iNum;
    buffer_entry *in_buffer_data = &file_handler[fi->fh].write_buf;

    if (in_buffer_data->start_byte > 0 && in_buffer_data->start_byte + in_buffer_data->byte_cnt != offset) {     // file buffer is not empty and current write is not continous.
        PRINT_WARNING("write: detect not continous write in write buffer");
        return -errno;
    }

    if (offset < (long int)mapping_table[iNum].actual_size_in_disk + in_buffer_data->byte_cnt) {
        PRINT_WARNING("write: currently not support data update.");
        return -errno;
    }

    if (in_buffer_data->byte_cnt == 0) in_buffer_data->start_byte = offset;

    size_t less_size = size;
    char * cur_buf_ptr = (char *)buf;
    mapping_table[iNum].logical_size_for_host += size;
    while (less_size > 0) {
        bool can_fill_buffer = in_buffer_data->byte_cnt + less_size >= MAX_GROUP_SIZE;
        if (can_fill_buffer){
            DEBUG_MESSAGE("  start write back file buffer");
            int write_into_buffer_size = MAX_GROUP_SIZE - in_buffer_data->byte_cnt;
            memcpy(in_buffer_data->content + in_buffer_data->byte_cnt, cur_buf_ptr, write_into_buffer_size);
            less_size -= write_into_buffer_size;
            cur_buf_ptr += write_into_buffer_size;
            int cut_pos = cut((const uint8_t*)in_buffer_data->content, MAX_GROUP_SIZE, ctx->mi, ctx->ma, ctx->ns,
                      ctx->mask_s, ctx->mask_l);
            #ifdef CAFTL
            cut_pos = BLOCK_SIZE; // use fixed chunking
            #endif
            DEBUG_MESSAGE("    cut pos: " << cut_pos << " byte cnt: " << in_buffer_data->byte_cnt);
            std::unique_lock<std::shared_mutex> unique_status_record_lock(status_record_mutex);
            total_write_size += cut_pos;
            unique_status_record_lock.unlock();
            // hashing
            char cur_fp[SHA_DIGEST_LENGTH];
            SHA1((const unsigned char *)in_buffer_data->content, cut_pos, (unsigned char *)cur_fp);
            FP_TYPE new_fp(cur_fp, SHA_DIGEST_LENGTH);
            // query fp store
            std::shared_lock<std::shared_mutex> shared_fp_store_lock(fp_store_mutex);
            auto fp_store_iter = fp_store.find(new_fp);
            shared_fp_store_lock.unlock();
            if (fp_store_iter != fp_store.end() && false){   // found
                DEBUG_MESSAGE("    found duplicate group!!");
                unique_status_record_lock.lock();
                total_dedup_size += cut_pos;
                unique_status_record_lock.unlock();
                fp_store_iter->second->ref_times += 1;
                mapping_table[iNum].group_pos.push_back(fp_store_iter->second);
                mapping_table[iNum].group_offset.push_back(in_buffer_data->start_byte);
                for(int i = mapping_table[iNum].group_idx.size(); i <= (in_buffer_data->start_byte + cut_pos) / BLOCK_SIZE; i++){
                    mapping_table[iNum].group_idx.push_back(mapping_table[iNum].group_pos.size() - 1);
                }
            }
            else{                                   // not found
                group_addr *new_group_addr = new group_addr;
                new_group_addr->iNum = iNum;
                new_group_addr->ref_times = 1;
                new_group_addr->start_byte = mapping_table[iNum].actual_size_in_disk;
                new_group_addr->group_length = cut_pos;
                int res = pwrite(file_handler[fi->fh].fh, in_buffer_data->content, cut_pos, mapping_table[iNum].actual_size_in_disk);
                if (res == -1){
                    PRINT_WARNING("write: write back to disk failed!!");
                    delete new_group_addr;
                    return -errno;
                }
                mapping_table[iNum].actual_size_in_disk += cut_pos;
                mapping_table[iNum].group_pos.push_back(new_group_addr);
                mapping_table[iNum].group_offset.push_back(in_buffer_data->start_byte);
                for(int i = mapping_table[iNum].group_idx.size(); i <= (in_buffer_data->start_byte + cut_pos) / BLOCK_SIZE; i++){
                    mapping_table[iNum].group_idx.push_back(mapping_table[iNum].group_pos.size() - 1);
                }
                std::unique_lock<std::shared_mutex> unique_fp_store_lock(fp_store_mutex);
                fp_store[new_fp] = new_group_addr;
                unique_fp_store_lock.unlock();
            }
            // update buffer
            if (cut_pos < MAX_GROUP_SIZE){
                //memcpy(in_buffer_data->content, in_buffer_data->content + cut_pos, MAX_GROUP_SIZE - cut_pos);
                for (char *i = in_buffer_data->content + cut_pos, *j = in_buffer_data->content; i < in_buffer_data->content + MAX_GROUP_SIZE; i++, j++){
                    *j = *i;
                }
            }
            in_buffer_data->start_byte += cut_pos;
            in_buffer_data->byte_cnt = MAX_GROUP_SIZE - cut_pos;
        }
        else{
            DEBUG_MESSAGE("start fill buffer");
            DEBUG_MESSAGE("in_buffer_data->byte_cnt: " << in_buffer_data->byte_cnt);
            DEBUG_MESSAGE("less_size: " << less_size);
            memcpy(in_buffer_data->content + in_buffer_data->byte_cnt, cur_buf_ptr, less_size);
            DEBUG_MESSAGE("end fill buffer");
            in_buffer_data->byte_cnt += less_size;
            less_size = 0;
        }
    }
    return size;
}

/*static int cdcfs_truncate(const char *path, off_t size) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("[truncate]" << path << " size: " << size);

    res = truncate(full_path, size);
    if (res == -1) {
        return -errno;
    }
    return 0;
}*/

static int cdcfs_ftruncate(const char *path, off_t size, fuse_file_info *fi) {
    int res;
    return 0;
    DEBUG_MESSAGE("[ftruncate]" << path << ", size: " << size);

    if  (fi == NULL) {
        return -errno;
    }

    res = ftruncate(file_handler[fi->fh].fh, size);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_unlink(const char *path) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("unlink: " << path);
    
    res = unlink(full_path);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_readlink(const char *path, char *buf, size_t size) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("[readlink]" << full_path);

    res = readlink(full_path, buf, size - 1);
    if (res == -1) {
        return -errno;
    } 
    else {
        buf[res] = '\0';
        return 0;
    }
}

static int cdcfs_link(const char *oldpath, const char *newpath) {
    int res;
    char full_old[1024];
    char full_new[1024];
    snprintf(full_old, sizeof(full_old), "%s%s", BACKEND, oldpath);
    snprintf(full_new, sizeof(full_new), "%s%s", BACKEND, newpath);
    DEBUG_MESSAGE("[link]" << "dest: " << full_new << " src: " << full_old);
    
    res = link(full_old, full_new);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_symlink(const char *oldpath, const char *newpath) {
    int res;
    char full_old[1024];
    char full_new[1024];
    snprintf(full_old, sizeof(full_old), "%s%s", BACKEND, oldpath);
    snprintf(full_new, sizeof(full_new), "%s%s", BACKEND, newpath);
    DEBUG_MESSAGE("[symlink]" << "dest" << full_new << " src: " << full_old);

    res = symlink(full_old, full_new);
    if (res == -1) {
        return -errno;
    }
    return 0;
}