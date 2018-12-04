/*
  Simple File System

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.

*/

#include "params.h"
#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"

/*
 * User defined data-structures
 */
#define BOOT_IDENTIFIER "U2Pn1KJCO4sVzNZuSxzGcVDP1YbULrAgxr0WKOZQncW4N3ETktEyjn9QTfypJNaJ5LYHUl2pI5YORqubjsPuopJVojWcPPq15L282kdczm8MLO7pEyiTYHIQqLnCnRUECYV1aQ82YHayHPgVuBXKhxaM2qdpfR9kcAi2MnYM8c3HKOSThVdaxyhGwtCnG8qxwPhyDRusYynVUqtgQotbUix2cTSi3v0VIB9seSxwgq1U2InEwHSQS"
#define MAX_BLOCKS 34
#define MAX_PATH 64
#define MAX_INODES 256
#define DISK_SIZE 16 * 1024 * 1024
typedef unsigned int uint;

typedef struct inode
{
    mode_t mode;
    //    uint is_dir : 1; //0-> file, 1-> directory
    long created, modified, accessed;
    uint link_count;
    uint size;
    uint uid, gid;
    int blocks[MAX_BLOCKS];
    uint *blocks_single;
    char path[MAX_PATH];
} inode; //256 bytes

#define TOTAL_BLOCKS DISK_SIZE / BLOCK_SIZE
#define INODE_BITMAP_START 1
#define DATA_BITMAP_START 2
#define INODE_BLOCK_START 10
#define DATA_BLOCK_START INODE_BLOCK_START + (MAX_INODES * sizeof(inode)) / BLOCK_SIZE
#define MAX_DATA_BLOCKS (TOTAL_BLOCKS - DATA_BLOCK_START) + 1

uint MAX_INODES_BLOCKS = MAX_INODES / (BLOCK_SIZE / sizeof(inode));

char inode_bitmap[MAX_INODES / 8];
char data_bitmap[MAX_DATA_BLOCKS / 8];

inode inode_list[MAX_INODES];

// TO_DO Write the structs to disk blocks

char *get_file_name(char *path)
{
    int len = strlen(path);
    if (len == 0)
        return NULL;
    if (len == 1)
        return path;
    int i = len - 1;
    for (; i >= 0; i--)
    {
        if (path[i] == '/')
            break;
    }
    char *temp;
    if (i >= 0)
    {
        temp = malloc(len - i);
        memcpy(temp, path + i + 1, len - i);
    }

    return temp;
}

int find_inode(char *path)
{
    int i;

    for (i = 0; i < MAX_INODES; i++)
    {
        if (inode_bitmap[i / 8] && (1 << (7 - (i % 8))))
        {
            if (strcmp(inode_list[i].path, path) == 0)
            {
                //log_msg("File found %s\n", path);
                return i;
            }
        }
    }

    return -1;
}

int substring(char *source, int from, int n, char *target){
    int length,i;
    //get string length
    length = strlen(source);

    if(from>length){
        printf("Starting index is invalid.\n");
        return 1;
    }

    if((from+n)>length){
        //get substring till end
        n=(length-from);
    }

    //get substring in target
    for(i=0;i<n;i++){
        target[i]=source[from+i];
    }
    target[i]='\0'; //assign null at last

    return 0;
}

int find_parent(char path[])
{
    int length = strlen(path);
    if (path == NULL || length == 0)
        return -1;

    if (length == 1)
        return 0;

    int i;
    int p = -1;
    for (i = length - 1; i >= 0; i--)
    {
        if (path[i] == '/')
        {
            p = 0;
            break;
        }
    }

    if (p == -1)
    {
        log_msg("Some weird shit \n");
        return -1;
    }

    if(i==0){
        return 0;
    }
    char parent_path[MAX_PATH];
    substring(path,0, i,parent_path);
    log_msg("parent path : %s\n", parent_path);

    int parent_index = find_inode(parent_path);

      if(parent_index > -1) {
          return parent_index;
      }
    return -1;
}


void print_inode()
{
    int i, j;

    char *block_buf = malloc(BLOCK_SIZE);
    block_read(INODE_BITMAP_START, block_buf);

    for (i = 0; i < 32; i++)
    {
        for (j = 1; j <= 8; j++)
        {
            if (!(block_buf[i] & (1 << (8 - j))))
            {
                log_msg("0");
            }
            else
                log_msg("1");
        }
        log_msg("\n");
    }
}

int get_first_unset_bit(char bitmap[])
{
    int i, j;
    int block_length = sizeof(bitmap);
    for (i = 0; i < block_length; i++)
    {
        for (j = 1; j <= 8; j++)
        {
            if (!(bitmap[i] & (1 << (8 - j))))
            {
                return ((i * 8) + j - 1);
            }
        }
    }

    return -1;
}

void set_bit(char bitmap[], int index)
{
    int map_index = index / 8;
    int shift = index % 8;
    bitmap[map_index] |= (1 << (7 - shift));
}

void unset_bit(char bitmap[], int index)
{
    int map_index = index / 8;
    int shift = index % 8;
    bitmap[map_index] &= ~(1 << (7 - shift));
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn)
{
    fprintf(stderr, "in bb-init\n");
    log_msg("\nsfs_init()\n");
    //log_msg("Size of inode : %d\n", sizeof(inode));
    log_conn(conn);
    log_fuse_context(fuse_get_context());

    char *block_buffer = malloc(BLOCK_SIZE);
    disk_open(SFS_DATA->diskfile);

    if (block_read(0, block_buffer) >= 0)
    {

        if (strcmp(BOOT_IDENTIFIER, block_buffer) == 0)
        {

            //log_msg("FS is already initialized\n");

            block_read(INODE_BITMAP_START, inode_bitmap);

            int j;
            for (j = DATA_BITMAP_START; j < DATA_BITMAP_START + 8; j++)
            {
                block_read(j, data_bitmap + (j - DATA_BITMAP_START) * BLOCK_SIZE);
            }

            for (j = 0; j < MAX_INODES / 2; j++)
            {
                block_read(j + INODE_BLOCK_START, (void *)(&inode_list) + j * BLOCK_SIZE);
            }
        }
        else
        {

            memset(inode_bitmap, 0, sizeof(inode_bitmap));
            memset(data_bitmap, 0, sizeof(data_bitmap));

            memset(block_buffer, 0, BLOCK_SIZE);

            inode *new_inode = (inode *)block_buffer;
            memset(new_inode->path, '/', 1);
            new_inode->mode = S_IFDIR | 0755;
            new_inode->blocks_single = NULL;
            new_inode->created = time(NULL);
            new_inode->modified = time(NULL);
            new_inode->accessed = time(NULL);
            new_inode->gid = getegid();
            new_inode->uid = getuid();
            new_inode->link_count = 2;
            new_inode->size = 0;

            set_bit(inode_bitmap, 0);
            memcpy(&inode_list[0], new_inode, sizeof(inode));
            block_write(INODE_BLOCK_START, block_buffer);

            void *temp = malloc(BLOCK_SIZE);
            strcpy(temp, BOOT_IDENTIFIER);

            block_write(0, temp);

            memset(block_buffer, '0', BLOCK_SIZE);
            memcpy(block_buffer, inode_bitmap, MAX_INODES / 8);
            block_write(INODE_BITMAP_START, block_buffer);

            int j;
            for (j = DATA_BITMAP_START; j < DATA_BITMAP_START + 8; j++)
            {
                block_write(j, data_bitmap + (j - DATA_BITMAP_START) * BLOCK_SIZE);
            }
        }
    }
    else
    {
        log_msg("Could not read the file\n");
    }

    free(block_buffer);

    return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata)
{
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
    disk_close();
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;

    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n",
            path, statbuf);

    memset(statbuf, 0, sizeof(struct stat));

    int index = find_inode(path);

    if (index >= 0)
    {
        inode *node = &inode_list[index];
        statbuf->st_mode = node->mode;
        statbuf->st_nlink = node->link_count;
        statbuf->st_uid = node->uid;
        statbuf->st_gid = node->gid;
        statbuf->st_ctime = node->created;
        statbuf->st_size = node->size;
        statbuf->st_atime = node->accessed;
        statbuf->st_mtime = node->modified;
    }
    else
    {
        //log_msg("Inode with path %s not found!\n", path);
        retstat = -ENOENT;
    }
    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
            path, mode, fi);

    if (find_inode(path) >= 0)
    {
        log_msg("File already exists\n");
        retstat = -EEXIST;
    }
    else
    {
        int free_block = get_first_unset_bit(data_bitmap);

        if (free_block > -1)
        {
            int free_inode = get_first_unset_bit(inode_bitmap);

            if (free_inode > -1)
            {
                inode *node = &inode_list[free_inode];
                strcpy(node->path, path);
                node->mode = S_IFREG | 0644;
                node->blocks_single = NULL;
                node->created = time(NULL);
                node->modified = time(NULL);
                node->gid = getegid();
                node->uid = getuid();
                node->link_count = 1;
                node->blocks[0] = free_block;
                node->size = 0;

                set_bit(inode_bitmap, free_inode);
                set_bit(data_bitmap, free_block);

                //Update parent inode
                int parent_index = find_parent(path);
                log_msg("Parent Found : %s\n", inode_list[parent_index].path);
                if (parent_index == -1)
                {
                    //log_msg("Some shit at index %d\n", parent_index);
                    retstat = -EFAULT; //TO_DO some other fault number
                    return retstat;
                }
                else
                {
                    inode *p = &inode_list[parent_index];
                    if (S_ISDIR(p->mode) == 0)
                    {
                        log_msg("Parent is not a directory\n");
                        //retstat = -EFAULT;
                        return errno;
                    }
                    p->link_count++;
                }

                block_write(INODE_BITMAP_START, inode_bitmap);
                block_write(DATA_BITMAP_START + free_block / BLOCK_SIZE, data_bitmap + free_block / BLOCK_SIZE * BLOCK_SIZE);
                block_write(INODE_BLOCK_START + free_inode / 2, &inode_list[(free_inode / 2) * 2]);
                block_write(INODE_BLOCK_START + parent_index / 2, &inode_list[(parent_index / 2) * 2]);
            }
            else
            {
                log_msg("Out of INODES\n");
                retstat = -EFAULT;
                return retstat;
            }
        }
        else
        {
            log_msg("Disk out of memory\n");
            retstat = -ENOMEM; //Disk out of memory
        }
    }

    //    print_inode();

    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);

    int node_index = find_inode(path);
    int parent_index = find_parent(path);

    inode *node, *parent;

    if (node_index >= 0)
    {
        node = &inode_list[node_index];
        parent = &inode_list[parent_index];

        if (S_ISREG(node->mode) != 0)
        {
            if (strcmp(node->path, parent->path) != 0)
            {
                unset_bit(inode_bitmap, node_index);
                block_write(INODE_BLOCK_START, inode_bitmap);

                parent->link_count--;
                int x, curr_data_block;
                for (x = 0; x < node->size; x++)
                {
                    curr_data_block = (node->blocks)[x];
                    unset_bit(data_bitmap, curr_data_block);
                    block_write(DATA_BLOCK_START + curr_data_block / BLOCK_SIZE, data_bitmap + curr_data_block / BLOCK_SIZE);
                }
            }
            else
            {
                log_msg("Cannot delete Root\n");
                retstat = -errno;
            }
        }
        else
        {
            retstat = - errno;
            log_msg("Not a File : %s\n", path);
        }
    }
    else
    {
        log_msg("Inode not found\n");
        retstat = -ENOENT;
    }

    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
            path, fi);

//    int fd = open(path, fi->flags);
//    log_msg("sfs_open : fd: %d\n", fd);
//    if (fd < 0)
//    {
//        log_msg("Unable to open file errorno: %d\n", errno);
//        return -errno;
//    }
//
//    fi->fh = fd;
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
            path, fi);

//    close(fi->fh);

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
            path, buf, size, offset, fi);

    int node_index = find_inode(path);
    inode *node = &inode_list[node_index];

    int start_block = offset / BLOCK_SIZE;
    offset = offset % BLOCK_SIZE;
    char *buf_offset = buf;
    int total_size_read = 0, size_to_read = 0;
    int curr_block = start_block;

    //log_msg("offset: %d  size: %d   node->size: %d\n", offset, size, node->size);
    if (offset > node->size)
    {
        retstat = -EFAULT;
        log_msg("sfs_read : Offset more than file size\n");
        return retstat;
    }
    else if ((offset + size) > node->size)
    {
        //log_msg("sfs_read : Resting size from %d to %d\n", size, node->size);
        size = node->size - offset;
    }

    void *block_buffer = malloc(BLOCK_SIZE);
    for (; total_size_read < size; curr_block++)
    {

        if (offset == 0)
        {
            size_to_read = BLOCK_SIZE;
        }
        else
        {
            size_to_read = BLOCK_SIZE - offset + 1;
        }

        if ((size - total_size_read) <= size_to_read)
        {
            size_to_read = size - total_size_read;
        }

        if (size_to_read != BLOCK_SIZE)
        {
            memset(block_buffer, '0', BLOCK_SIZE);
        }

        //log_msg("Reading from block : %d\n", node->blocks[curr_block]);
        block_read(DATA_BLOCK_START + node->blocks[curr_block], block_buffer);

        memcpy(buf_offset, block_buffer + offset, size_to_read);

        total_size_read += size_to_read;
        buf_offset = buf + total_size_read;
        offset = 0;
    }

    // Update accessed Time
//    node->accessed = time(NULL);
//    block_write(INODE_BLOCK_START + node_index / 2, &inode_list[(node_index / 2) * 2]);

    free(block_buffer);
    retstat = total_size_read;
    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
    //    print_inode();

    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
            path, buf, size, offset, fi);

    int node_index = find_inode(path);
    inode *node = &inode_list[node_index];
    int orig_offset = offset;

    int start_block = offset / BLOCK_SIZE;
    offset = offset % BLOCK_SIZE;
    char *buf_offset = buf;
    int total_size_written = 0, size_to_write = 0;
    int curr_block = start_block;
    void *block_buffer = malloc(BLOCK_SIZE);

    int file_max_blocks = node->size / BLOCK_SIZE;
    int free_block;

    //log_msg("total_size_read : %d   size : %d\n", total_size_written, size);
    //TODO : Put limit on curr_block
    for (; total_size_written < size; curr_block++)
    {
//        if(curr_block >= MAX_BLOCKS){
//            log_msg("Max file size reached\n");
//            return -EFAULT;
//        }

        if (curr_block > file_max_blocks)
        {
            free_block = get_first_unset_bit(data_bitmap);

            if (free_block > -1)
            {
                set_bit(data_bitmap, free_block);
                block_write(DATA_BITMAP_START + free_block / BLOCK_SIZE, data_bitmap + free_block / BLOCK_SIZE * BLOCK_SIZE);
                node->blocks[curr_block] = free_block;
                file_max_blocks++;
            }
            else
            {
                log_msg("sfs_write : Disk out of memory\n");
                retstat = -ENOMEM; //Disk out of memory
                return retstat;
            }
        }

        //log_msg("Writing in block : %d\n", DATA_BLOCK_START + node->blocks[curr_block]);

        if (offset == 0)
        {
            size_to_write = BLOCK_SIZE;
        }
        else
        {
            size_to_write = BLOCK_SIZE - offset + 1;
        }

        if ((size - total_size_written) <= size_to_write)
        {
            size_to_write = size - total_size_written;
        }

        if (size_to_write != BLOCK_SIZE)
        {
            block_read(DATA_BLOCK_START + node->blocks[curr_block], block_buffer);
        }

        memcpy(block_buffer + offset, buf_offset, size_to_write);
        block_write(DATA_BLOCK_START + node->blocks[curr_block], block_buffer);

        total_size_written += size_to_write;
        buf_offset = buf + total_size_written;
        offset = 0;
    }

    free(block_buffer);
    retstat = total_size_written;

    if ((orig_offset + size) > node->size)
    {
        node->size = orig_offset + size;
    }

    // Update accessed and modified Time
//    node->accessed = time(NULL);
//    node->modified = time(NULL);
    block_write(INODE_BLOCK_START + node_index / 2, &inode_list[(node_index / 2) * 2]);

    //    print_inode();

    return retstat;
}

/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
            path, mode);

    time_t t = time(NULL);
    int index = get_first_unset_bit(inode_bitmap);
    if(index > -1) {
        inode* node = &inode_list[index];
        set_bit(inode_bitmap,index);
        memcpy(node->path, path, strlen(path));   // set inode 0 as root by default
        node->mode = S_IFDIR | 0755;
        node->size = 0;
        node->created = t;
        node->link_count = 2;
        node->uid = getuid();
        node->gid = getegid();
        block_write(INODE_BITMAP_START,inode_bitmap);
        block_write(INODE_BLOCK_START + index / 2, &inode_list[(index / 2) * 2]);
    } else{
        log_msg("Directory creation failed..\n");
        retstat =  -ENOENT;
    }
    return retstat;
}

/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
            path);

    int dir_index = find_inode(path);

    if(dir_index > -1){
        inode *dir = &inode_list[dir_index];

        if(S_ISDIR(dir->mode) != 0){

            if(dir->link_count > 2){
                log_msg("Directory contains files deletion aborted\n");
                return -EPERM;
            }

            unset_bit(inode_bitmap,dir_index);
            block_write(INODE_BITMAP_START,inode_bitmap);

        }else{
            log_msg("%s not a directory\n",path);
            retstat = -errno;
        }

    }else{
        log_msg("Directory with path %s not found\n",path);
        retstat = -errno;
    }

    return retstat;
}

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n",
            path, fi);

    int index = find_inode(path);

    if (index >= 0)
    {
        inode *node = &inode_list[index];
        if (S_ISDIR(node->mode) == 0)
        {
            log_msg("File is not a directory\n");
            retstat = -EPERM;
        }
    }
    else
    {
        retstat = -ENOENT;
    }

    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nsfs_readdir(path=\"%s\", fi=0x%08x)\n",
            path, fi);

    //    print_inode();

    if (find_inode(path) < 0)
    {
        retstat = -ENOENT;
        return retstat;
    }

    //fill for the buffer
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    int i = 0;

    for (; i < MAX_INODES; i++)
    {
        if (inode_bitmap[i / 8] & 1 << (7 - (i % 8)))
        {
            inode *node = &inode_list[i];
            int parent_index = find_parent(node->path);

            inode *parent = &inode_list[parent_index];

            //check all children and root not equal to itself
            if (strcmp(parent->path, path) == 0 && strcmp(path, node->path) != 0)
            {
                struct stat *statbuf = malloc(sizeof(struct stat));
                statbuf->st_mode = node->mode;
                statbuf->st_nlink = node->link_count;
                statbuf->st_uid = node->uid;
                statbuf->st_gid = node->gid;
                statbuf->st_ctime = node->created;
                statbuf->st_size = node->size;
                statbuf->st_atime = node->accessed;
                statbuf->st_mtime = node->modified;

                filler(buf, get_file_name(node->path), statbuf, 0);
            }
        }
    }
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_releasedir(path=\"%s\", fi=0x%08x)\n",
            path, fi);
    return retstat;
}

struct fuse_operations sfs_oper = {
    .init = sfs_init,
    .destroy = sfs_destroy,

    .getattr = sfs_getattr,
    .create = sfs_create,
    .unlink = sfs_unlink,
    .open = sfs_open,
    .release = sfs_release,
    .read = sfs_read,
    .write = sfs_write,

    .rmdir = sfs_rmdir,
    .mkdir = sfs_mkdir,

    .opendir = sfs_opendir,
    .readdir = sfs_readdir,
    .releasedir = sfs_releasedir};

void sfs_usage()
{
    fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct sfs_state *sfs_data;

    // sanity checking on the command line
    if ((argc < 3) || (argv[argc - 2][0] == '-') || (argv[argc - 1][0] == '-'))
        sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL)
    {
        perror("main calloc");
        abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = argv[argc - 2];
    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    sfs_data->logfile = log_open();

    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

    return fuse_stat;
}
