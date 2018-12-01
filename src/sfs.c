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
#define IDENTIFIER "U2Pn1KJCO4sVzNZuSxzGcVDP1YbULrAgxr0WKOZQncW4N3ETktEyjn9QTfypJNaJ5LYHUl2pI5YORqubjsPuopJVojWcPPq15L282kdczm8MLO7pEyiTYHIQqLnCnRUECYV1aQ82YHayHPgVuBXKhxaM2qdpfR9kcAi2MnYM8c3HKOSThVdaxyhGwtCnG8qxwPhyDRusYynVUqtgQotbUix2cTSi3v0VIB9seSxwgq1U2InEwHSQS"
#define MAX_BLOCKS 33
#define MAX_PATH 128
#define MAX_INODES 256
#define DISK_SIZE 16 * 1024 * 1024
typedef unsigned int uint;

typedef struct inode
{
    mode_t permissions;
    uint is_dir : 1; //0-> file, 1-> directory
    long created, modified, accessed;
    uint link_count;
    uint size;
    uint uid, gid;
    uint blocks[MAX_BLOCKS];
    uint *blocks_single;
    char path[MAX_PATH];
} inode; //256 bytes

/* typedef struct superblock{
    char identifier[256] = IDENTIFIER;
} */

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
    if (i > 0)
    {
        temp = malloc(len - i);
        memcpy(temp, path + i + 1, len - i);
    }

    return temp;
}

inode *find_inode(char *path)
{
    int i;

    for (i = 0; i < MAX_INODES; i++)
    {
        if (inode_bitmap[i / 8] && (1 << (7 - (i % 8))))
        {
            if (strcmp(inode_list[i].path, path) == 0)
            {
                log_msg("File found %s\n", path);
                return &inode_list[i];
            }
        }
    }

    return NULL;
}

inode *find_parent(char path[])
{
    if (path == NULL)
        return NULL;

    int length = sizeof(path);

    if (length == 1)
        return &inode_list[0];

    int i;
    char parent[MAX_PATH];
    inode *p;
    for (i = length - 1; i >= 0; i--)
    {
        if (path[i] == '/')
        {
            break;
        }
    }

    if (i > 0)
    {
        strncpy(parent, path, i + 1);
        p = find_inode(parent);
    }
    return p;
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
                log_msg("\nclear bit found at (%d,%d) %d\n",
                        i, j);
                return ((i * 8) + j);
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

    log_conn(conn);
    log_fuse_context(fuse_get_context());

    char *block_buffer = malloc(BLOCK_SIZE);
    disk_open(SFS_DATA->diskfile);

    if (block_read(0, block_buffer) >= 0)
    {

        if (strcmp(IDENTIFIER, block_buffer) == 0)
        {

            log_msg("FS is already initialized\n");
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
            new_inode->permissions = S_IFDIR | 0755;
            new_inode->blocks_single = NULL;
            new_inode->created = time(NULL);
            new_inode->modified = time(NULL);
            new_inode->accessed = time(NULL);
            new_inode->is_dir = 1;
            new_inode->gid = getegid();
            new_inode->uid = getuid();
            new_inode->link_count = 2;
            new_inode->size = 0;

            set_bit(inode_bitmap, 0);
            memcpy(&inode_list[0], new_inode, sizeof(inode));

            block_write(INODE_BLOCK_START, &inode_list);

            void *temp = malloc(BLOCK_SIZE);
            strcpy(temp, IDENTIFIER);

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

    inode *node = find_inode(path);

    if (node != NULL)
    {
        statbuf->st_mode = node->permissions;
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
        log_msg("Inode with path %s not found!\n", path);
        retstat = ENOENT;
        return retstat;
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

    if (find_inode(path) != NULL)
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
                node->permissions = S_IFREG | 0644;
                node->blocks_single = NULL;
                node->created = time(NULL);
                node->modified = time(NULL);
                node->is_dir = 0;
                node->gid = getegid();
                node->uid = getuid();
                node->link_count = 1;
                node->blocks[0] = free_block;
                node->size = 1;

                //Update parent inode
                inode *p = find_parent(path);
                if (p == NULL)
                {
                    log_msg("Some shit \n");
                    retstat = -EFAULT; //TO_DO sme other fault number
                }
                else
                {
                    if (p->is_dir == 0)
                    {
                        log_msg("Parent is not a directory\n");
                        retstat = -EFAULT;
                    }
                    p->link_count++;
                }
            }
            else
            {
                log_msg("Out of INODES\n");
                retstat = -EFAULT;
            }
        }
        else
        {
            retstat = -ENOMEM; //Disk out of memory
        }
    }

    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);

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
    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
            path, buf, size, offset, fi);

    return retstat;
}

/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
            path, mode);

    return retstat;
}

/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
            path);

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

    inode *node = find_inode(path);

    if (node != NULL)
    {
        log_msg("Directory file found\n");
        if (node->is_dir == 0)
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

    log_msg("Reading directory......\n");

    //fill for the buffer
    //filler(buf, ".", NULL, 0);
    //filler(buf, "..", NULL, 0);
    log_msg("FUCK OFFF %d\n", MAX_INODES);
    int i;
    for (i = 0; i < 256; i++)
    {
        log_msg("i : %d", i);
        if (inode_bitmap[i / 8] && 1 << (7 - (i % 8)))
        {
            log_msg("IN i : %d", i);
            inode *node = &inode_list[i];
            struct stat *statbuf = malloc(sizeof(struct stat));
            statbuf->st_mode = node->permissions;
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

    log_msg("Done reading directory....\n");
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;

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
