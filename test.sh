#!/bin/bash

if [ -z "$1" ]; then
  echo "using $0 DIR"
  exit 1
fi

rm -rf test

TDIR="$1"
PASS="1"
PART=104857600
SECT=4096
RES="EMPTY RES"
SPEED=0
BREAKED=0

make || exit 0
trap "echo SIGINT;BREAKED=1;" SIGINT
mkdir test
cd test
DIR=$(pwd)

mkdir hole pre post crypt split http mnt
echo -e "\n\n\n\n\n\n"| openssl req -x509 -nodes -days 1095 -newkey rsa:2048 -out server.crt -keyout server.key 2>/dev/null
/usr/sbin/apache2 -d .. -f test.conf

touch img
../bin/rsbt-hole img hole
../bin/rsbt-pre / pre
SIZE=$(du -hbs "pre/$TDIR" | cut -f1 )

echo
(cd "pre/$TDIR"; mksquashfs . $DIR/hole/file -noD -noF -noappend -processors 1 -no-sparse 2>/dev/null)
echo

while mount | grep $DIR/hole > /dev/null; do fusermount -u hole; [ $? -eq 0 ] && break; sleep 1; done
while mount | grep $DIR/pre > /dev/null; do fusermount -u pre; [ $? -eq 0 ] && break; sleep 1; done
sync


SIZEORIG=$(stat -c "%s" img)
SIZE=$((($SIZEORIG + $SECT - 1)/$SECT*$SECT))
SIZEDD=$(($SIZE - $SIZEORIG))
dd if=/dev/urandom bs=$SIZEDD count=1 >>img 2>/dev/null
echo -e "dd fix:\t\tsize = "$(stat -c "%s" img)


../bin/rsbt-post img post
../bin/rsbt-crypt post/file /dev/mapper/crypt_dev crypt -o allow_other

dd if=/dev/zero of=header bs=2066432 count=1 2>/dev/null
echo -n "$PASS" | /sbin/cryptsetup  --key-file - --header header -c aes-xts-plain64 --use-urandom -s 512 -h sha512 luksFormat crypt/file

echo -n "$PASS" | sudo /sbin/cryptsetup --key-file - --header header luksOpen crypt/file crypt_dev
sleep 3
sudo chown $(id -u) /dev/mapper/crypt_dev
chmod 200 /dev/mapper/crypt_dev

ls crypt/.connect 2>/dev/null

../bin/rsbt-split crypt/file $PART split
../bin/rsbt-http https://127.0.0.1:4443/file%d $(ls split | wc -l) http -o allow_other

echo -n "$PASS" | sudo /sbin/cryptsetup --key-file - --header header luksOpen http/file crypt_dev_test
sleep 3
sudo chown $(id -u) /dev/mapper/crypt_dev_test
chmod 400 /dev/mapper/crypt_dev_test


SPACE="\r%"$(tput cols)"s\r"
sudo mount /dev/mapper/crypt_dev_test -o ro mnt
if [ $? -eq 0 ]; then
	(cd "$TDIR";  find . -readable -type f > $DIR/list)
	echo
	SIZE=$((($SIZE + 1048576)/1024))
	
	(
		TIMES=1
		NOW=0
		UPDATE=1
		SPEED=0
		iostat /dev/mapper/crypt_dev_test $UPDATE | while read line; do
			ADD=$(echo $line | grep ^dm |  sed 's/  */ /g' | cut -f5 -d' ')
			[ -z "$ADD" ] && continue
			NOW=$(($NOW + $ADD))
			SPEED_NOW=$(($ADD / $UPDATE))
			if [ $TIMES -eq 1 ]; then SPEED_NOW=0; fi
			if [ $TIMES -eq 4 ]; then SPEED=$SPEED_NOW; fi
			SPEED=$((($SPEED * 95  + $SPEED_NOW * 5) / 100))
			TIMES=$(($TIMES + 1))
			WAIT=$((($SIZE-$NOW) / ($SPEED + 1)))
			PROC=$(($NOW * 100 / $SIZE))
			printf "$SPACE                $PROC%%  ( $NOW kb / $SIZE kb ) [ $SPEED kb/s ] [ $WAIT sec ] [ $TIMES sec ]"
		done & echo $! > stat.pid
	)


	while read line; do 
		[ -n "$2" ] && printf "$SPACE\t\t$line\n"
		cmp "$TDIR/$line" "mnt/$line"
		RES=$?
		[ $RES -ne 0 ] && break
		[ $BREAKED -ne 0 ] && break
	done < list
	
	kill $(cat stat.pid) 2>/dev/null
    
    while mount | grep $DIR/mnt > /dev/null; do sudo umount mnt; [ $? -eq 0 ] && break; sleep 1; done
fi

 sync
 sudo /sbin/cryptsetup close crypt_dev_test

 while mount | grep $DIR/http > /dev/null; do fusermount -u http; [ $? -eq 0 ] && break; sleep 1; done
 while mount | grep $DIR/split > /dev/null; do fusermount -u split; [ $? -eq 0 ] && break; sleep 1; done
 ls crypt/.disconnect 2>/dev/null

 sync
 sudo /sbin/cryptsetup close crypt_dev

 while mount | grep $DIR/crypt > /dev/null; do fusermount -u crypt; [ $? -eq 0 ] && break; sleep 1; done
 while mount | grep $DIR/post > /dev/null; do fusermount -u post; [ $? -eq 0 ] && break; sleep 1; done
 kill $(cat apache.pid)
 cd ..

printf "$SPACE                [[  res = $RES  ]]\n"
exit $RES
