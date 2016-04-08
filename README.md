RS Backup Tools
===============

About
-----

RS Backup Tools is a set of tools to prepare your data to uploading to free cloud and use it after uploading.
It decides main problem of backuping to clouds - privacy of data.

Why I write this tools? Because in one day I wanted to backup my set of films and music and info to cloud. One of the reasons was no space on disk. And I took great think - put all this files to clouds. But I didn't want to share my information with cloud provider. So I had several problems - file size restructions, crypto representation of uncrypted files and access to this files after uploading.

So, this tool set consist of 4 parts:
- **Fs** - represents your directory as one file (using squashfs or tar) without needing extra space on disk (but need some time to sending an array of zeros..) and you cannot change files in that directory, when you backuping.
- **Crypt** - represents file as crypted using you favorite tool cryptsetup (I named this part *"vice versa cryptsetup"*)
- **Split** - reperesents file as group of files with fixed size (need to overcome restrictions of file sizes)
- **Http** - reperesents remote file on webserver as local (like *httpfs*, but easily and clearer (250 vs 1150 lines) and written on curl (more stable and so mush opportunities to growing)

-----------------------------------------------------------------

**WARNING: YOU MUST UNDERSTAND WHAT YOU DO.  
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND**.

-----------------------------------------------------------------

Compile && Install
------------------

Tested for *i386* and *amd64*.
You need also [squashfs v4.2](https://sourceforge.net/projects/squashfs/files/squashfs/squashfs4.2/squashfs4.2.tar.gz/download).

```
$ sudo apt-get install build-essential libfuse-dev
$ sudo apt-get install libcurl4-openssl-dev libssl-dev
$ make
$ make install prefix=/usr
```

Fs
--

You must use fs that supports sparse files. *Ext2,3,4* are good.   
First we create squashfs, that contains selected folder, but fs will be virtual and it will not take place. For example you can use */usr/bin*, or change *DIR*

```
$ DIR=/usr/bin
$ mkdir tmp tmp/hole tmp/pre tmp/post
$ touch tmp/img
$ rsbt-hole tmp/img tmp/hole
$ rsbt-pre / tmp/pre
$ mksquashfs tmp/pre/$DIR tmp/hole/file \
-noD -noF -noappend -processors 1 -no-sparse
$ fusermount -u tmp/pre
$ fusermount -u tmp/hole
$ sync
$ rsbt-post tmp/img tmp/post -o allow_other
```

After this you have *tmp/post/file*, that looks like you create squashfs without this all tools, but...

```
$ du -h tmp/img
9,0M	tmp/img
$ du -h --apparent-size tmp/img
599M	tmp/img
```

And if you select directory with films without small files, you have something like 10Mb on 10GB.
BUT. It take time to send big arrays of zeros and limits by memory speed.  
On my computer this is 200 MB/s. And 10Gb takes 50 sec.  

Now I describe how it's works.  
rsbt-pre represents real fs, but files consists only zeros and magic string with description, where file is and what it size is.

```
$ rsbt-pre / tmp/pre
$ hexdump -C tmp/pre/bin/bash
00000000  2b 40 40 40 40 40 20 72  73 65 72 31 39 31 31 20  |+@@@@@ rser1911 |
00000010  47 51 50 47 4d 36 54 38  4d 32 31 57 39 52 39 33  |GQPGM6T8M21W9R93|
00000020  37 4d 4f 4f 36 46 4c 56  31 34 36 53 41 4c 49 4c  |7MOO6FLV146SALIL|
00000030  55 52 4b 52 30 56 44 56  43 41 56 53 52 51 43 39  |URKR0VDVCAVSRQC9|
00000040  53 4a 52 42 56 36 39 47  36 33 35 4d 33 38 47 32  |SJRBV69G635M38G2|
00000050  52 5a 38 38 51 45 4b 51  4b 34 33 30 41 44 35 31  |RZ88QEKQK430AD51|
00000060  59 35 41 42 52 49 56 51  56 48 53 38 43 58 33 42  |Y5ABRIVQVHS8CX3B|
00000070  49 52 38 33 20 40 40 40  40 40 40 39 37 35 34 38  |IR83 @@@@@@97548|
00000080  38 3a 2f 62 69 6e 2f 62  61 73 68 00 00 00 00 00  |8:/bin/bash.....|
00000090  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000ee280
$ fusermount -u tmp/pre
```

Now it's time for rsbt-hole. This tool using sparse file as backend and you can do with it same things like with normal file with one exception - when you write zeros to the end on file or to space fulled with zeros, you take successful result, but there is no real write to backend.

```
$ touch tmp/test
$ rsbt-hole tmp/test tmp/hole
$ dd if=/dev/zero of=tmp/hole/file count=128 bs=1M 2>/dev/null
$ fusermount -u tmp/hole
hole.c:		size = 134217728
$ sync
$ du -h tmp/test
12K	tmp/test
$ du -h --apparent-size tmp/test
128M	tmp/test
$ rm tmp/test
```

You see? 128 Mb of zeros "costs" 12K of file.

And on the finished step we processing our file with *rsbt-post*, that read *magic* marks and represents file like normal created fs with data in files.  
You can check this, if you mount *tmp/post/file* to some directory. (*"user_allow_other"* needed for access from root to *tmp/post/file*, see *Test* part).

```
$ mkdir tmp/mnt
$ sudo mount tmp/post/file -o ro tmp/mnt
$ diff -s tmp/mnt/seq /usr/bin/seq
$ sudo umount tmp/mnt
```

Crypt
-----

This is main part, because in some cases you not need to crate fs representation of folder, but want to upload full drive or partition.
May be only this part you really need.  
So let's start this *"encryption on the fly"* or *"vice versa cryptsetup"*.

It works like this:
1. user or kernel asks data from *rsbt-crypt* file
-  *rsbt-crypt* reads data from asked offset and asked length from origin file to buffer
-  *rsbt-crypt* writes from buffer to backend cryptsetup device, opend with *O_SYNC*
-  *rsbt-crypt* waits, while kenel write back crypted buffer
-  *rsbt-crypt* returns data to asker

First you must unmount *rsbt-post* and align you image: (*cryptsetup* trims uncompleted sector)

```
$ fusermount -u tmp/post
$ SIZE=$(stat -c "%s" tmp/img)
$ SIZE=$((($SIZE + 4096 - 1)/4096*4096 - $SIZE))
$ dd if=/dev/zero bs=$SIZE count=1 >>tmp/img 2>/dev/null
```

Next mount *rsbt-post* and *rsbt-crypt*:

```
$ mkdir tmp/crypt
$ rsbt-post tmp/img tmp/post
$ rsbt-crypt tmp/post/file /dev/mapper/crypt_dev tmp/crypt -o allow_other
```

Next you need to prepare *LUKS header* (enter YES and you password for backup):

```
$ dd if=/dev/zero of=tmp/header bs=2066432 count=1 2>/dev/null
$ /sbin/cryptsetup -v --header tmp/header -c aes-xts-plain64 \
--use-random -s 512 -h sha512 luksFormat tmp/crypt/file
```

After this you can create backend for our *rsbt-crypt*:

```
$ sudo /sbin/cryptsetup -v --header tmp/header luksOpen tmp/crypt/file crypt_dev
$ sudo chown $(id -u) /dev/mapper/crypt_dev
$ chmod 200 /dev/mapper/crypt_dev
```

Say to *rsbt-crypt* that backend is ready now:

```
$ ls tmp/crypt/.connect 2>/dev/null
```

Now in *tmp/crypt/file* we have crypted representation of *tmp/post/file*. Like it was wrote on device, prepared with cryptsetup. But real data stay uncrypted!  
You can test spee like that:

```
$ dd if=tmp/crypt/file of=/dev/null bs=1M count=100
100+0 records in
100+0 records out
104857600 bytes (105 MB) copied, 6.00808 s, 17.5 MB/s
```

Or can mount it:

```
$ mkdir tmp/mnt
$ sudo /sbin/cryptsetup --header tmp/header luksOpen tmp/crypt/file crypt_dev_test
$ sudo mount /dev/mapper/crypt_dev_test -o ro tmp/mnt
```

Split
-----

I think every cloud set limits to file size and if we want to upload big file, we must split it to parts.  
This small fs do this simple work - you point it to file and set part size and see you file as collection of parts in mount point.  
For example, divide our file to part of 100Mb size.

```
$ mkdir tmp/split
$ rsbt-split tmp/crypt/file 104857600 tmp/split
split.c:	size = 627183616, parts = 6, part = 104857600, last = 102895616
$ ls -la tmp/split/
-r--r--r-- 1 root  root  104857600 янв  1  1970 file0
-r--r--r-- 1 root  root  104857600 янв  1  1970 file1
-r--r--r-- 1 root  root  104857600 янв  1  1970 file2
-r--r--r-- 1 root  root  104857600 янв  1  1970 file3
-r--r--r-- 1 root  root  104857600 янв  1  1970 file4
-r--r--r-- 1 root  root  102895616 янв  1  1970 file5
```

Http
----

This part using for remote access for our backup.  
We must get direct url for our parts from cloud (sharing by link, or may be using cookies.. curl easy-using in this moment).  
For local test you must add you dir to web server (*ln -s `pwd`/tmp/split /var/www/test*) or see *test.sh*.

```
$ mkdir tmp/http
$ rsbt-http https://$URL/test/file%d $PARTS tmp/http
```

After successful connection you take file in *tmp/http/file* and can use in with cryptsetup.

```
$ sudo /sbin/cryptsetup --header tmp/header luksOpen tmp/http/file crypt_dev_test
$ sudo mount /dev/mapper/crypt_dev_test -o ro tmp/mnt
```

Test
----

You can test full system with script named *test.sh*.  
This script creates and tests tar array and shows all things that was described before.  

You need *iostat*, *mksquashfs*, *apache2*, *cmp*, *cryptsetup*, *openssl*, *fusermount*, *sudo*  
You also need to set *"user_allow_other"* in */etc/fuse.conf* (or you can start script from root, but... =) )

```
$ sudo sed 's/#user_allow_other/user_allow_other/' -i /etc/fuse.conf
```

Root rights need only for *cryptsetup* and *chown* on created devices. For more info explore *test.sh*.

Run test with command like this:

```
$ ./test.sh /var/cache/apt/archives
g++ -Wall -D_FILE_OFFSET_BITS=64 -D_XOPEN_SOURCE=700 -D_ISOC99_SOURCE -I/usr/include/fuse -lfuse -o bin/rsbt-post post.cpp
gcc  -Wall -D_FILE_OFFSET_BITS=64 -D_XOPEN_SOURCE=700 -D_ISOC99_SOURCE -I/usr/include/fuse -lfuse -o bin/rsbt-hole hole.c
gcc  -Wall -D_FILE_OFFSET_BITS=64 -D_XOPEN_SOURCE=700 -D_ISOC99_SOURCE -I/usr/include/fuse -lfuse -o bin/rsbt-pre pre.c
gcc  -Wall -D_FILE_OFFSET_BITS=64 -D_XOPEN_SOURCE=700 -D_ISOC99_SOURCE -I/usr/include/fuse -lfuse -o bin/rsbt-crypt crypt.c
gcc  -Wall -D_FILE_OFFSET_BITS=64 -D_XOPEN_SOURCE=700 -D_ISOC99_SOURCE -I/usr/include/fuse -lfuse -o bin/rsbt-split split.c
gcc  -Wall -D_FILE_OFFSET_BITS=64 -D_XOPEN_SOURCE=700 -D_ISOC99_SOURCE -I/usr/include/fuse -lfuse -lcurl -o bin/rsbt-http http.c

Parallel mksquashfs: Using 1 processor
Creating 4.0 filesystem on /home/user/test/hole/file, block size 131072.
[===========================================================================================================================] 4576/4576 100%
Exportable Squashfs 4.0 filesystem, gzip compressed, data block size 131072
	uncompressed data, compressed metadata, uncompressed fragments, compressed xattrs
	duplicates are removed
Filesystem size 559411.96 Kbytes (546.30 Mbytes)
	99.99% of uncompressed filesystem size (559445.96 Kbytes)
Inode table size 6567 bytes (6.41 Kbytes)
	22.07% of uncompressed inode table size (29751 bytes)
Directory table size 5929 bytes (5.79 Kbytes)
	37.95% of uncompressed directory table size (15625 bytes)
Number of duplicate files found 1
Number of inodes 373
Number of files 371
Number of fragments 81
Number of symbolic links  0
Number of device nodes 0
Number of fifo nodes 0
Number of socket nodes 0
Number of directories 2
Number of ids (unique uids + gids) 1
Number of uids 1
	root (0)
Number of gids 1
	root (0)

hole.c:		size = 572837888
dd fix:		size = 572837888
post.cpp:	size = 572837888
crypt.c:	size = 572837888
split.c:	size = 572837888, parts = 6, part = 104857600, last = 48549888
http.c:		size = 572837888, parts = 6, part = 104857600, last = 48549888

                [[  res = 0, speed = 20757 kb/s  ]]                              

```

Or you can run script like that to show compared files

```
$ ./test.sh /var/cache/apt/archives show_files
```

Or test on big number little files

```
$ ./test.sh /usr/share/man show_files
```

Known Issues
------------

Please report issues in the issue tracker.

License
-------

GPL 3  
Copyright (C) 2016 rser1911 <rser1911@gmail.com>
