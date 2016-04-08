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

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <strings.h>
#include <pthread.h>

#define DEBUG_THREADS
#ifdef DEBUG_THREADS
	static int fd_printf;
	int main_old(int, char **);
	int main(int argc, char ** argv) { fd_printf = dup(0); return main_old(argc, argv); }
	#define main(a,b) main_old(a,b)
	#define printf(format, ...) dprintf(fd_printf, __FILE__ ":\t\t" format, __VA_ARGS__)
#endif

#define MAX_BUF 	64 * 1024
static int 			fd_main;
static off_t 		file_size;


static int
fuse_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
	
	if (offset >= file_size) return 0;
	if (offset + (off_t) size > file_size) size = file_size - offset;
	
	if (size == 0) return 0;
	return pread(fd_main, buf, size, offset);
}

static int
fuse_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {

	static __thread char tmp_buf[MAX_BUF];
	static char zerod[MAX_BUF] = {0};
				
	off_t file_size_save = file_size;
	if (offset + (off_t) size > file_size)
		file_size = offset + (off_t) size;
		
	if (memcmp(buf, zerod, size) == 0){
		if (offset >= file_size_save)
			return size;
			
		int res = pread(fd_main, tmp_buf, size, offset);
		if (res >= 0 && memcmp(tmp_buf, zerod, res) == 0){
			return size;
		}
	}
	
	return pwrite(fd_main, buf, size, offset);
}

/* ****************** */

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
		stbuf->st_size = file_size;
		return 0;
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
	if (strcmp(path, "/file") == 0) \
		return 0;
	return -ENOENT;
}

static int 
fuse_release(const char *path, struct fuse_file_info *fi) {
	return 0;
}

static int
fuse_truncate(const char *path, off_t off){
	return 0;
}


struct fuse_operations my_operations = {
	.getattr     = fuse_getattr,
	.readdir     = fuse_readdir,
	.open        = fuse_open,
	.release     = fuse_release,
	.read        = fuse_read,
	.write       = fuse_write,
	.truncate    = fuse_truncate
};

int main(int argc, char* argv[]) {
	if (argc < 3){
		printf("%s file mnt {else fuse params}\n", argv[0]); 
		return 1;
	}
	
	fd_main = open(argv[1], O_RDWR);
	if (fd_main == -1){
		printf("Cann't open file %s\n", argv[1]);
		return 1;
	}
	
	struct stat st;
	if (fstat(fd_main, &st) == -1){
		printf("Cann't stat file %s\n", argv[1]);
		return 1;		
	}
	
	if (!S_ISREG(st.st_mode))
		ioctl(fd_main, BLKGETSIZE64, &st.st_size);
	
	file_size = st.st_size;

	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	fuse_opt_add_arg(&args, argv[0]);
	fuse_opt_add_arg(&args, "-o");
	fuse_opt_add_arg(&args, "max_write=65536");
	fuse_opt_add_arg(&args, "-s");

	int i;
	for (i = 2; i < argc; ++i)
		fuse_opt_add_arg(&args, argv[i]);		
	int res_main = fuse_main(args.argc, args.argv, &my_operations, NULL);
	
	// write last
	char buf[1] = {0};
	int res = pread(fd_main, buf, 1, file_size - 1);
	if (res == 0)
		pwrite(fd_main, buf, 1, file_size - 1);
		
	printf("size = %llu\n", (long long int )file_size);
	return res_main;
}
