/*

	R-Ser Backup Tools
	Copyright (C) 2016 rser1911 <rser1911@gmail.com>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	
*/

#define FUSE_USE_VERSION 26

#include <stdio.h>
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <pthread.h>

#define DEBUG_THREADS
#ifdef DEBUG_THREADS
	static int fd_printf;
	int main_old(int, char **);
	int main(int argc, char ** argv) { fd_printf = dup(0); return main_old(argc, argv); }
	#define main(a,b) main_old(a,b)
	#define printf(format, ...) dprintf(fd_printf, __FILE__ ":\t" format, __VA_ARGS__)
#endif

#define MAX_BUF 	64 * 1024
#define SECTOR 		4096
#define MASK  		(SECTOR - 1)
#define NAME_SIZE 	1024
#define SHOW_REQ	0

#define my_off_t long long int

static my_off_t 	orig_size;
static int 			orig_fd;
static int 			mapping_fd = -1;
static char 		mapping_name[NAME_SIZE];
pthread_mutex_t 	work_mutex = PTHREAD_MUTEX_INITIALIZER; // 1 thread for read
pthread_mutex_t 	crypto_mutex = PTHREAD_MUTEX_INITIALIZER; // write lock	
pthread_cond_t  	crypto_cond = PTHREAD_COND_INITIALIZER; // wait for all

static char * 		crypto_buf;
static my_off_t	    crypto_buf_used;
static my_off_t 	crypto_buf_size;
static my_off_t 	crypto_offset;


static int
fuse_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
			 
	if (mapping_fd == -1)
		return 0;
		 
	if (SHOW_REQ) printf("read from %lld size %lld\n", (my_off_t) offset, (my_off_t) size);
	if (offset >= orig_size) return 0;
	if (offset + size > orig_size) size = orig_size - offset;
	if (size == 0) return 0;
	
	static __thread char tmp[MAX_BUF];
	static __thread char crypto_tmp[MAX_BUF];
	
	size_t size_save = size;
	my_off_t offset_save = offset;
	my_off_t res;
	
	offset = offset & ( ~ (off_t) (SECTOR - 1) ); 
	size += offset_save - offset;
	size = (size +  SECTOR - 1) & ( ~ (off_t) (SECTOR - 1) );
	
	if (SHOW_REQ && (offset != offset_save || size != size_save)) 
		printf("read from %lld size %lld (changed)\n", (my_off_t) offset, (my_off_t) size);
	
	pread(orig_fd, tmp, size, offset);
	
	pthread_mutex_lock(&work_mutex);
	{
		crypto_buf = crypto_tmp;
		crypto_buf_size = size;
		crypto_buf_used = 0;
		crypto_offset = offset;
			
		res = pwrite(mapping_fd, tmp, size, offset);
		if (res != size){
			printf("BUG write crypt res = %lld size = %lld\n", res, (my_off_t) size);	
			pthread_mutex_unlock (&work_mutex);
			return 0;
		}
		
		pthread_mutex_lock (&crypto_mutex);
		while(crypto_buf_used != crypto_buf_size)
			pthread_cond_wait (&crypto_cond, &crypto_mutex);
		pthread_mutex_unlock (&crypto_mutex);
		
		memcpy(buf, crypto_tmp + (offset_save - offset), size_save);
	}
	pthread_mutex_unlock(&work_mutex);
	return size_save;

}

static int
fuse_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {

	if (SHOW_REQ) printf("write to %lld size %lld\n",  (my_off_t) offset, (my_off_t) size);
	if (size & MASK || offset & MASK){ // used to find MAX_BUF, max_read
		printf("MASK --- write to %lld size %lld\n", (my_off_t) offset, (my_off_t) size);
		return -EPERM;
	}
	
	offset -= crypto_offset;
	
	if (offset < 0 || offset + size > MAX_BUF){
		printf("PANIC WRITE off %lld size %lld, cr off %lld cr size %lld\n", offset + crypto_offset, (my_off_t) size, crypto_offset, crypto_buf_used);
	}else{
		memcpy(crypto_buf + offset, buf, size);
		
		pthread_mutex_lock (&crypto_mutex);
		{
			crypto_buf_used += size;
			if (crypto_buf_used == crypto_buf_size){
				pthread_cond_signal(&crypto_cond);
			}
		}
		pthread_mutex_unlock (&crypto_mutex);
	}
	
	return size;
}

/* =========== ROUTINE  =========== */

static int
fuse_getattr(const char *path, struct stat *stbuf) {
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) { 
		stbuf->st_mode = S_IFDIR | 0555;
		stbuf->st_nlink = 2 + 1;
		return 0;
	}
	
	if (strcmp(path, "/file") == 0) {
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_nlink = 1;
		stbuf->st_size = orig_size;
		return 0;
	}
	
	if (strcmp(path, "/.connect") == 0){
		if (mapping_fd != -1)
			return -ENOENT;
		
		mapping_fd = open(mapping_name, O_WRONLY | O_SYNC);
		
		if (mapping_fd == -1){
			printf("Cann't open mapping file %s\n", mapping_name);
		}
		
		return -ENOENT;
	}
	if (strcmp(path, "/.disconnect") == 0){
		if (mapping_fd == -1)
			return -ENOENT;
			
		mapping_fd = close(mapping_fd);
		
		if (mapping_fd == -1){
			printf("Cann't close mapping file %s\n", mapping_name);
		}
		
		mapping_fd = -1;
		return -ENOENT;
	}
	return -ENOENT;
}

static int
fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi) {
	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0); 
	filler(buf, "..", NULL, 0);
	filler(buf, "file", NULL, 0);
	return 0;
}

static int
fuse_open(const char *path, struct fuse_file_info *fi) {
	if (strcmp(path, "/file") == 0)
			return 0;
	return -ENOENT;
}

static int 
fuse_release(const char *path, struct fuse_file_info *fi) {
	return 0;
}

struct fuse_operations my_operations = {
	.getattr     = fuse_getattr,
	.readdir     = fuse_readdir,
	.open        = fuse_open,
	.release     = fuse_release,
	.read        = fuse_read,
	.write       = fuse_write
};

int main(int argc, char* argv[]) {
	if (argc < 4){
		printf("%s orig mapping point\n", argv[0]); 
		return 1;
	}
	
	orig_fd = open(argv[1], O_RDONLY);
	if (orig_fd == -1){
		printf("Cann't open file %s\n", argv[1]);
		return 1;
	}
	
	strncpy(mapping_name, argv[2], NAME_SIZE - 1);
	
	struct stat st;
	if (fstat(orig_fd, &st) == -1){
		printf("Cann't stat file %s\n", argv[1]);
		return 1;		
	}
	
	if (!S_ISREG(st.st_mode))
		ioctl(orig_fd, BLKGETSIZE64, &st.st_size);
		
	orig_size = st.st_size;
	printf("size = %lld\n", orig_size);
	
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	fuse_opt_add_arg(&args, argv[0]);
	fuse_opt_add_arg(&args, "-o");
	fuse_opt_add_arg(&args, "direct_io,max_read=65536");
	int i;
	for (i = 3; i < argc; ++i)
		fuse_opt_add_arg(&args, argv[i]);	
		
	return fuse_main(args.argc, args.argv, &my_operations, NULL);
}
