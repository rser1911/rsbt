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
#include <sys/mman.h>
#include <string.h>
#include <map>
#include <syscall.h>

#define DEBUG_THREADS
#ifdef DEBUG_THREADS
	static int fd_printf;
	int main_old(int, char **);
	int main(int argc, char ** argv) { fd_printf = dup(0); return main_old(argc, argv); }
	#define main(a,b) main_old(a,b)
	#define printf(format, ...) dprintf(fd_printf, __FILE__ ":\t" format, __VA_ARGS__)
#endif

static char magic[] = "@@@@@@ rser1911 GQPGM6T8M21W9R937MOO6FLV146SALILURKR0VDVCAVSRQC9SJRBV69G635M38G2RZ88QEKQK430AD51Y5ABRIVQVHS8CX3BIR83 @@@@@@";
static int  magic_size = sizeof(magic) - 1;
static off_t file_size;
static int fd_orig;

struct desc {
	char * name;
	off_t start;
	int id;
};

typedef struct desc desc_t;
std::map<off_t, desc_t *> file_map;

#define BUF_SIZE 4096 * 4
#define MIN_SIZE 4096

static int
fuse_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
				  
	if (offset >= file_size) return 0;
	if (offset + (off_t) size > file_size) size = file_size - offset;
	if (size == 0) return 0;
	
	std::map<off_t, desc_t *>::iterator it;
	it = file_map.upper_bound(offset);
	 
	off_t save_size = size;
	for (;;){
		off_t size_now = size;
		if (offset + (off_t) size > it->first)
			size_now = it->first - offset + 1;
			
		if (it->second->name != NULL){
			static __thread int id_now = -1;
			static __thread int fd_now = -1;
			if (it->second->id != id_now){
				if (fd_now != -1)
					close(fd_now);
				
				fd_now = open(it->second->name, O_RDONLY);
				if (fd_now == -1){
					id_now = -1;
					return 0;
				}
				id_now = it->second->id;
			}
				
			off_t res = pread(fd_now, buf, size_now, offset - it->second->start);
			if (res == -1){
				printf("[post] bug in reading, errno = %d\n", errno);
				return -errno;
			}else if(res < size_now){
				printf("[post] bug in reading, small res = %lld size = %lld\n", (long long int) res, (long long int) size_now);
				memset(buf + res, 0, size_now - res);
			}
		}else{
			off_t res = pread(fd_orig, buf, size_now, offset);
			if (res != size_now){
				printf("[post] bug in reading orig, res = %lld\n", (long long int) res);
				return -EIO;
			}
		}
		
		if (size_now == (off_t) size)
			break;
			
		size -= size_now;
		offset += size_now;
		buf += size_now;
		++it;
	}
	
	return save_size;
}


/* ***** */

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

int main(int argc, char* argv[]) {
	magic[0] = '+';
	static struct fuse_operations my_operations;
	my_operations.getattr     = fuse_getattr;
	my_operations.readdir     = fuse_readdir;
	my_operations.open        = fuse_open;
	my_operations.release     = fuse_release;
	my_operations.read        = fuse_read;

	if (argc < 3){
		printf("%s file point\n", argv[0]); 
		return 1;
	}
	
	fd_orig = open(argv[1], O_RDONLY);
	if (fd_orig == -1){
		printf("Cann't open file %s\n", argv[2]);
		return 1;
	}
	
	struct stat st;
	if (fstat(fd_orig, &st) == -1){
		printf("Cann't stat file %s\n", argv[1]);
		return 1;		
	}
	
	if (!S_ISREG(st.st_mode))
		ioctl(fd_orig, BLKGETSIZE64, &st.st_size);
	
	file_size = st.st_size;
	printf("size = %llu\n", (long long int) file_size);
	
	off_t o = 0, o_empty = 0;
	int id_now = 0;
	int res;
	desc_t * new_desc;
	char buf[BUF_SIZE];
	
	for (;;){
		res = pread(fd_orig, buf, BUF_SIZE, o);
		if (res < MIN_SIZE){
			if (res > 0){
				new_desc = (desc_t *) malloc(sizeof(desc_t));
				new_desc->name = NULL;
				new_desc->start = o_empty;
				file_map[file_size - 1] = new_desc;
			}
			break;	
		}
		
		char * now;
		int o_buf = 0;
		res -= (magic_size - 1);
		
		for (;;){
			now = (char *) memchr(buf + o_buf, *magic, res - o_buf);
			if (now == NULL) break;
			o_buf = now - buf;
			if (memcmp(now, magic, magic_size) == 0) break;
			++o_buf;
		}
		
		if (now == NULL){
			o += res;
			continue;
		}
		
		o += o_buf;
		res = pread(fd_orig, buf, MIN_SIZE, o);
		if (res != MIN_SIZE){
			printf("Corupped file.. res = %d, must be %d\n", res, MIN_SIZE);
			return 1;
		}
		
		off_t len = strtoll(buf + magic_size, NULL, 0);
		char * p = (char *) memchr(buf + magic_size, ':', MIN_SIZE - magic_size) + 1;
		// TODO: checking p
		
		if (o_empty < o){
			new_desc = (desc_t *) malloc(sizeof(desc_t));
			new_desc->name = NULL;
			new_desc->start = o_empty;
			file_map[o - 1] = new_desc;
		}
		
		new_desc = (desc_t *) malloc(sizeof(desc_t));			
		new_desc->id = ++id_now;
		new_desc->name = (char *) malloc(strlen(p) + 1);
		strcpy(new_desc->name, p);
		new_desc->start = o;
		file_map[o + len - 1] = new_desc;
		//printf("%lld %s\n", len, p);
		
		o += len;
		o_empty = o;	
	}
	
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	fuse_opt_add_arg(&args, argv[0]);
	int i;
	for (i = 2; i < argc; ++i)
		fuse_opt_add_arg(&args, argv[i]);		
	return fuse_main(args.argc, args.argv, &my_operations, NULL);
}
