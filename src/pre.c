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

#include <dirent.h>
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

static char orig[1024];
static int orig_len;

#define SET(x)  (x | 1l<<63)
#define GET(x)  (x & ~ (1l<<63))
#define TST(x)	(x & 1l<<63)

#define MINSIZE 4096
static char magic[] = "@@@@@@ rser1911 GQPGM6T8M21W9R937MOO6FLV146SALILURKR0VDVCAVSRQC9SJRBV69G635M38G2RZ88QEKQK430AD51Y5ABRIVQVHS8CX3BIR83 @@@@@@";
#define my_off_t long long int

struct my_file {
	int fd;
	int flag;
	char * str;
	my_off_t len;
};

static int 
fuse_release(const char *path, struct fuse_file_info *fi) {
	struct my_file * f = (struct my_file *) (uintptr_t) fi->fh;
	int res = close(f->fd);
	free(f);
	if (res == -1)
		return -errno;
	return 0;
}

static int
fuse_open(const char *path, struct fuse_file_info *fi) {
	
	char tmp_path[1024];
	memcpy(tmp_path, orig, orig_len);
	strcpy(tmp_path + orig_len, path);
	
	int fd = open(tmp_path, O_RDONLY);
	if (fd == -1)
		return -errno;
		
	struct stat st;
	if (fstat(fd, &st) == -1){
		return -1;		
	}
	
	fi->fh = (intptr_t) malloc(sizeof(struct my_file));
	struct my_file * f = (struct my_file *) (uintptr_t) fi->fh;
	f->fd = fd;
	if (st.st_size >= MINSIZE){
		f->flag = 1;
		f->str = malloc(1024);
		sprintf(f->str, "%s%lld:%s", magic, (my_off_t) st.st_size, path);
		f->len = strlen(f->str);
	}else{
		f->flag = 0;
	}
	return 0;
}

static int
fuse_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
				  
	struct my_file * f = (struct my_file *) (uintptr_t) fi->fh;
	int copy_size = 0;
	
	if (f->flag && offset < f->len){
		copy_size = f->len - offset;
		if (copy_size > size) copy_size = size;
		memcpy(buf, f->str + offset, copy_size);
		size -= copy_size;
		if (size == 0) 
			return copy_size;
		offset += copy_size;
		buf += copy_size;
	}
	
	if (!f->flag){ 
		int res = pread(f->fd, buf, size, offset);
		if (res == -1){
			printf("read error %lld %lld %s\n", (my_off_t) size, (my_off_t) offset, f->str);
			return -errno;
		}
		return res + copy_size;
	} else {
		memset(buf, 0x00, size);
		return size + copy_size;
	}
}

static int
fuse_getattr(const char *path, struct stat *stbuf) {
	
	char tmp_path[1024];
	memcpy(tmp_path, orig, orig_len);
	strcpy(tmp_path + orig_len, path);
	
	if (lstat(tmp_path, stbuf) == -1)
		return -errno;
	return 0;
}

static int 
fuse_readlink (const char *path, char *buf, size_t bufsiz){
	char tmp_path[1024];
	memcpy(tmp_path, orig, orig_len);
	strcpy(tmp_path + orig_len, path);
	
	int res = readlink(tmp_path, buf, bufsiz -1);
	if (res == -1) return -errno;
	buf[res] = '\0';
	return 0;
}

static int
fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi) {
				
	DIR *dp = (DIR *) (uintptr_t) fi->fh;
    struct dirent *de;

    de = readdir(dp);
    if (de == 0)
        return -errno;
        
    do {
        if (filler(buf, de->d_name, NULL, 0) != 0)
            return -ENOMEM;
    } while ((de = readdir(dp)) != NULL);
    
    return 0;
}

static int 
fuse_opendir(const char *path, struct fuse_file_info * fi){
	char tmp_path[1024];
	memcpy(tmp_path, orig, orig_len);
	strcpy(tmp_path + orig_len, path);
	
	DIR * res = opendir(tmp_path);
	if (res == NULL) return -errno;
	fi->fh = (intptr_t) res;
	return 0;
}

static int 
fuse_releasedir(const char *path, struct fuse_file_info * fi){
	int res = closedir((DIR *) (uintptr_t) fi->fh);
	if (res == -1) return -errno;
	return 0;
}


struct fuse_operations my_operations = {
	.readdir     = fuse_readdir,
	.opendir     = fuse_opendir,
	.releasedir  = fuse_releasedir,
	.getattr     = fuse_getattr,
	.open        = fuse_open,
	.release     = fuse_release,
	.read        = fuse_read,
	.readlink    = fuse_readlink,
};

int main(int argc, char* argv[]) {
	magic[0] = '+';
	if (argc < 3){
		printf("%s orig point\n", argv[0]); 
		return 1;
	}
	
	strcpy(orig, argv[1]);
	orig_len = strlen(orig);
	
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	fuse_opt_add_arg(&args, argv[0]);
	int i;
	for (i = 2; i < argc; ++i)
		fuse_opt_add_arg(&args, argv[i]);		
	return fuse_main(args.argc, args.argv, &my_operations, NULL);
}
