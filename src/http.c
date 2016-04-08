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
#include <curl/curl.h>

#define DEBUG_THREADS
#ifdef DEBUG_THREADS
	static int fd_printf;
	int main_old(int, char **);
	int main(int argc, char ** argv) { fd_printf = dup(0); return main_old(argc, argv); }
	#define main(a,b) main_old(a,b)
	#define printf(format, ...) dprintf(fd_printf, __FILE__ ":\t\t" format, __VA_ARGS__)
#endif

#define NAME_SIZE 	1024
#define my_off_t long long int

static my_off_t file_size;
static my_off_t part_size;
static int parts;
static char url[NAME_SIZE];

struct curl_buf {
  char * mem;
  my_off_t used;
  my_off_t size;
};

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void * userp){
	size = size * nmemb;
	struct curl_buf * buf = (struct curl_buf *) userp;
	if (buf->used + size <= buf->size){
		memcpy(buf->mem + buf->used, ptr, size);
		buf->used += size;
		return size;
	}
	printf("bad read %llu, used %llu, size %llu\n", (my_off_t) size, buf->used, buf->size);
	return 0;
}

static int
fuse_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
				  
	if (offset >= file_size) return 0;
	if (offset + (off_t) size > file_size) size = file_size - offset;
	if (size == 0) return 0;
	ssize_t res_size = 0;
	int num, num_end;
	
	for (;;){
		num = offset / part_size;
		num_end = (offset + size - 1) / part_size;
		if (num == num_end) break;
		
		size_t size_now = (num + 1) * part_size - offset;
		int res = fuse_read(path, buf, size_now, offset, fi);
		if (res != size_now){
			return 0;
		}
		buf += size_now;
		offset += size_now;
		size -= size_now;
		res_size += size_now;
	}
	
	char tmp[NAME_SIZE];
	snprintf(tmp, NAME_SIZE, url, num);
	offset -= num * part_size;
	
	int retry;
	for ( retry = 3; retry; --retry){
		static __thread CURL *curl = NULL;
		if (!curl) curl = curl_easy_init();
		
		curl_easy_setopt(curl, CURLOPT_URL, tmp);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
		
		struct curl_buf buf_now ;
		buf_now.mem = buf;
		buf_now.size = size;
		buf_now.used = 0;
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &buf_now);
		
		char range[128];
		sprintf(range, "%lld-%lld", (my_off_t) offset, (my_off_t) offset + size - 1);
		curl_easy_setopt(curl, CURLOPT_RANGE, range);
	
		CURLcode res = curl_easy_perform(curl);
		//curl_easy_cleanup(curl);
		
		if(res != CURLE_OK) {
			printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			continue;
		}
		
		if (buf_now.size != buf_now.used){
			printf("fuse_read size %llu != used %llu\n", buf_now.size, buf_now.used);
			continue;
		}
		
		res_size += size;
		break;
	}
	
	if (!retry)
		return 0;
	
	return res_size;
}

static size_t get_size(char * url){
	double filesize = 0.0;
	
	CURL *curl;
	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	CURLcode res = curl_easy_perform(curl);

	if(CURLE_OK == res) {
		res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD,
							  &filesize);
		if(!(CURLE_OK == res) || !(filesize>0.0))
			filesize = -1;
	}
	else
		printf("get_size && curl told us %d\n", res);

	curl_easy_cleanup(curl);
	return (size_t) filesize;
}

/************************/

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


struct fuse_operations my_operations = {
	.getattr     = fuse_getattr,
	.readdir     = fuse_readdir,
	.open        = fuse_open,
	.release     = fuse_release,
	.read        = fuse_read,
};


int main(int argc, char* argv[]) {
	if (argc < 4){
		printf("%s url count mnt {else fuse params}\n", argv[0]); 
		return 1;
	}
	
	curl_global_init(CURL_GLOBAL_ALL);
	strncpy(url, argv[1], sizeof(url) - 1);
	
	char *err_strpol;
	parts =  strtol(argv[2] ,&err_strpol, 0);
	if (*err_strpol != '\0' && parts <= 0){
		printf("bad parts %s\n", argv[2]);
		return 1;
	}
	
	char tmp[NAME_SIZE];
	snprintf(tmp, NAME_SIZE, url, parts - 1);
	file_size = part_size = get_size(tmp);
	if (part_size == -1){
		printf("bad size %s\n", tmp);
		return 1;
	}
	
	if (parts > 1){
		snprintf(tmp, NAME_SIZE, url, 0);
		part_size = get_size(tmp);
		if (part_size == -1){
			printf("bad size %s\n", tmp);
			return 1;
		}
		file_size += (parts - 1) * part_size;
	}
	
	printf("size = %llu, parts = %d, part = %llu, last = %llu\n", file_size, parts, part_size, file_size - (parts - 1) * part_size);
	
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	fuse_opt_add_arg(&args, argv[0]);
	fuse_opt_add_arg(&args, "-s"); // single, not need blocking

	int i;
	for (i = 3; i < argc; ++i)
		fuse_opt_add_arg(&args, argv[i]);		
	int res = fuse_main(args.argc, args.argv, &my_operations, NULL);
	curl_global_cleanup();
	return res;
}
	
