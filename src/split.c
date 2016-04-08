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

#define DEBUG_THREADS
#ifdef DEBUG_THREADS
	static int fd_printf;
	int main_old(int, char **);
	int main(int argc, char ** argv) { fd_printf = dup(0); return main_old(argc, argv); }
	#define main(a,b) main_old(a,b)
	#define printf(format, ...) dprintf(fd_printf, __FILE__ ":\t" format, __VA_ARGS__)
#endif

#define my_off_t long long int
static my_off_t last_part_size, part_size;
static int parts = 0;
static int fd_main;

static int
fuse_getattr(const char *path, struct stat *stbuf) {
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) { 
		stbuf->st_mode = S_IFDIR | 0555;
		stbuf->st_nlink = 2 + parts;
		return 0;
	}
	
	if (memcmp(path, "/file", 5) == 0 && path[5] != '\0'){
		char * ptr;
		long ret = strtol(path + 5, &ptr, 10);
		if(*ptr == '\0' && ret < parts && ret >= 0) {
			stbuf->st_mode = S_IFREG | 0444;
			stbuf->st_nlink = 1;
			
			stbuf->st_size = part_size;
			if (ret == parts - 1)
				stbuf->st_size = last_part_size;
			return 0;
		}
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
	int i;
	char name[128];
	for (i = 0; i < parts; ++i){
		sprintf(name, "file%d", i);
		filler(buf, name, NULL, 0);
	}
	return 0;
}

static int
fuse_open(const char *path, struct fuse_file_info *fi) {
	if (memcmp(path, "/file", 5) == 0 && path[5] != '\0'){
		char *ptr;
		long ret = strtol(path + 5, &ptr, 10);
		if(*ptr == '\0' && ret < parts && ret >= 0) {
			fi->fh = ret;
			return 0;
		}
	}
	return -ENOENT;
}

static int 
fuse_release(const char *path, struct fuse_file_info *fi) {
	return 0;
}

static int
fuse_truncate(const char *path, off_t off){
	return -ENOENT;
}


static int
fuse_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
				  
	off_t now_part_size = part_size;
	if (fi->fh == parts - 1) now_part_size = last_part_size;
	if (offset >= now_part_size) return 0;
	if (offset + size > now_part_size)
		size = now_part_size - offset;
	if (size == 0) return 0;
	return pread(fd_main, buf, size, fi->fh * part_size + offset);
}

static int
fuse_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
		return -EPERM;
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
	
	if (argc < 4){
		printf("%s file size_part mnt {else fuse params}\n", argv[0]); 
		return 1;
	}
	
	fd_main = open(argv[1], O_RDONLY);
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
	
	char *err_strpol;
	part_size =  strtol(argv[2] ,&err_strpol, 0);
	if (*err_strpol != '\0' && part_size <= 0){
		printf("bad size %s\n", argv[2]);
		return 1;
	}
	
	parts = (st.st_size + part_size - 1) / part_size;
	last_part_size = st.st_size - (parts - 1) * part_size;

	printf("size = %lld, parts = %d, part = %lld, last = %lld\n", (my_off_t) st.st_size, parts, part_size, last_part_size);
	
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	fuse_opt_add_arg(&args, argv[0]);
	int i;
	for (i = 3; i < argc; ++i)
		fuse_opt_add_arg(&args, argv[i]);		
	return fuse_main(args.argc, args.argv, &my_operations, NULL);
}
