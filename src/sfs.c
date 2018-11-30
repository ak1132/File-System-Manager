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
#define MAX_BLOCKS 35
#define MAX_PATH 128
#define MAX_INODES 256
#define DISK_SIZE 16 * 1024 * 1024
typedef unsigned int uint;

void *inode_bitmap;
void *data_bitmap;

uint TOTAL_BLOCKS;
uint INODE_BITMAP_START = 0;
uint DATA_BITMAP_START = 1;
uint INODE_BLOCK_START = 9;
uint DATA_BLOCK_START = 137;

uint MAX_DATA_BLOCKS;
uint MAX_INODES_BLOCKS;

typedef struct inode
{
	mode_t permissions;
	uint is_dir : 1; //0-> file, 1-> directory
	long created, modified;
	uint link_count;
	uint size;
	uint uid, gid;
	uint blocks[MAX_BLOCKS];
	uint *blocks_single;
	char path[MAX_PATH];
} inode; //256 bytes

inode *find_inode(char *path)
{
	int i;
	void *block_buffer = malloc(BLOCK_SIZE);
	for (i = INODE_BLOCK_START; i < INODE_BLOCK_START + MAX_INODES/(BLOCK_SIZE/ sizeof(inode)); i++)
	{

		if (block_read(i, block_buffer))
		{
			inode *i1 = (inode *) block_buffer;
			inode *i2 = (inode *) ((void *)block_buffer + sizeof(inode));
//
			if (strcmp((i1)->path, path) == 0)
			{
				i1 = malloc(sizeof(inode));
				memcpy(i1, block_buffer, sizeof(inode));
				free(block_buffer);
				return i1;
			}
//
			if (strcmp((i2)->path, path) == 0)
			{
				i2 = malloc(sizeof(inode));
				memcpy(i2, block_buffer + sizeof(inode), sizeof(inode));
				free(block_buffer);
				return i2;
			}
		}
	}
	free(block_buffer);
	return NULL;
}

void set_bit(int base_block_num, int bit_index)
{
	char *block_buffer = malloc(BLOCK_SIZE);
	int byte_index;
	int block_num = base_block_num + bit_index/8/BLOCK_SIZE;
	if (block_read(block_num, block_buffer))
	{
		byte_index = (bit_index % BLOCK_SIZE) / 8;
		block_buffer[byte_index] = block_buffer[byte_index] || 1 << (7 - ((bit_index % BLOCK_SIZE) % 8));
//		block_write(block_num, block_buffer);
//		free(block_buffer);
	}
}

void unset_bit(int base_block_num, int bit_index)
{
	char *block_buffer = malloc(BLOCK_SIZE);
	int byte_index;
	int block_num = base_block_num + bit_index/8/BLOCK_SIZE;
	if (block_read(block_num, block_buffer))
	{
		byte_index = (bit_index % BLOCK_SIZE) / 8;
		block_buffer[byte_index] = block_buffer[byte_index] && (0xFF - (1 << (7 - ((bit_index % BLOCK_SIZE) % 8))));
//		block_write(block_num, block_buffer);
//		free(block_buffer);
	}
}

int first_unset_bit(int base_block, int blocks)
{
	char *block_buffer = malloc(BLOCK_SIZE);
	int i,j;
	int curr_block = 0;

	for(curr_block = 0; curr_block < blocks; curr_block++)
	{
		if (block_read(base_block + curr_block, block_buffer))
		{
			for(j = 0; j < BLOCK_SIZE*8; j++)
			{
				if(block_buffer[j/8] && 1 << (7-j%8) == 0)
				{
					free(block_buffer);
					return curr_block * BLOCK_SIZE * 8 + j;
				}
			}

		}else
		{
			log_msg("Could not read Block : %d", curr_block);
		}
	}

	free(block_buffer);
	return -1;
}

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

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
    // TODO: Write code to check if disk needs to be reformatted

	fprintf(stderr, "in bb-init\n");
	log_msg("\nsfs_init()\n");

	log_conn(conn);
	log_fuse_context(fuse_get_context());

	disk_open(SFS_DATA->diskfile);
	//struct stat *stat_buf = malloc(sizeof(struct stat));
	//lstat(SFS_DATA->diskfile, stat_buf);

	//log_stat(stat_buf);

	TOTAL_BLOCKS = DISK_SIZE / BLOCK_SIZE;
	MAX_DATA_BLOCKS = TOTAL_BLOCKS - DATA_BLOCK_START + 1;
	MAX_INODES_BLOCKS = MAX_INODES/(BLOCK_SIZE/ sizeof(inode));
	DATA_BLOCK_START = INODE_BLOCK_START + (MAX_INODES * sizeof(inode)) / BLOCK_SIZE;

	inode_bitmap = malloc(MAX_INODES/8);
	memset(inode_bitmap, 0, MAX_INODES/8);
	data_bitmap = malloc()

	void *block_buffer = malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	int i;
	for (i = 0; i < INODE_BLOCK_START; i++)
	{
		block_write(i, block_buffer);
	}

	// Create dir inode for root path '/'
	int inode_index = first_unset_bit(INODE_BLOCK_START, MAX_INODES_BLOCKS);
	inode *new_inode = (void *)INODE_BLOCK_START + inode_index * sizeof(inode);
	new_inode->path = "/";
	new_inode->permissions = S_IFDIR | 0755;
	new_inode->blocks = 0;
	new_inode->blocks_single = NULL;
	new_inode->created = time(NULL);
	new_inode->is_dir = 1;
	new_inode->gid = getegid();
	new_inode->uid = getuid();
	new_inode->link_count = 2;

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

	// Write inode bitmap block to disk
	block_write(INODE_BITMAP_START,inode_bitmap);

	// Write data bitmap block to disk
	int curr_block;
	void *data_bmp_ptr = data_bitmap;

	for(curr_block = DATA_BITMAP_START; curr_block < INODE_BLOCK_START; curr_block++)
	{
		block_write(curr_block,data_bmp_ptr + (curr_block - DATA_BITMAP_START) * BLOCK_SIZE);
	}
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
	char fpath[MAX_PATH];

	strcpy(fpath, SFS_DATA->diskfile);
	strcat(fpath, "/");
	strcat(fpath, path);

	//initialize things to zero
	memset(statbuf, 0, sizeof(struct stat));

	//find the inode with the path
	inode *node = find_inode(path);

	if (node != NULL)
	{
		statbuf->st_mode = node->permissions;
		statbuf->st_nlink = node->link_count;
		statbuf->st_uid = node->uid;
		statbuf->st_gid= node->gid;
		statbuf->st_ctime = node->created;
		statbuf->st_size = node->size;
	}else{
		log_msg("inode with path %s not found!",path);
		retstat = ENOENT;
	}

	log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n", path, statbuf);

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
	log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n", path, mode,
			fi);

	// Find empty data block
	int data_block_index = first_unset_bit(DATA_BLOCK_START, MAX_DATA_BLOCKS);
	if(data_block_index == -1)
	{
		log_msg("sfs_create: Empty datablock not found!!!\n");
		return -1;
	}

	int inode_index = first_unset_bit(INODE_BITMAP_START, MAX_INODES_BLOCKS);
	if(inode_index == -1)
	{
		log_msg("sfs_create: Empty inode not found!!!\n");
		return -1;
	}

	inode *new_inode = INODE_BLOCK_START * BLOCK_SIZE;

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
	log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n", path, fi);

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
	log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n", path, fi);

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
int sfs_read(const char *path, char *buf, size_t size, off_t offset,
			 struct fuse_file_info *fi)
{
	int retstat = 0;
	log_msg(
		"\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
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
	log_msg(
		"\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
		path, buf, size, offset, fi);

	return retstat;
}

/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
	int retstat = 0;
	log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n", path, mode);

	return retstat;
}

/** Remove a directory */
int sfs_rmdir(const char *path)
{
	int retstat = 0;
	log_msg("sfs_rmdir(path=\"%s\")\n", path);

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
	log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n", path, fi);

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
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
				off_t offset, struct fuse_file_info *fi)
{
	int retstat = 0;

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

struct fuse_operations sfs_oper = {.init = sfs_init, .destroy = sfs_destroy,

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
	fprintf(stderr,
			"usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
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
