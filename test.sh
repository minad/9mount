#!/bin/sh

P9MOUNT=./9mount

expect() {
	addr=$1
	opts=$(echo $2 |tr , '\n' |sort |tr '\n' , |sed 's/,$//')
	mtpt=$(eval 'echo $'$#)
	expected="mount -t 9p -o $opts $addr $mtpt"
	shift; shift
	actual=$($P9MOUNT -n "$@" 2>&1)
	aopts=$(echo $actual |sed 's/.*-o \([^ ]*\) .*/\1/' |tr , '\n' |sort |tr '\n' , |sed 's/,$//')
	actual=$(echo $actual |sed 's/-o [^ ]*/-o '"$aopts"'/')
	if [ "$expected" != "$actual" ]; then
		echo '	'9mount "$@"
		echo $expected' #expected'
		echo $actual' #actual'
		exit 1
	fi
}

mtpt=/tmp/9mount
mkdir -p $mtpt
trap 'rmdir $mtpt' EXIT
dfltopts="uname=$USER,noextend,nodevmap"

expect ::1 $dfltopts,trans=tcp,port=888 localhost:888 $mtpt
expect 127.0.0.1 $dfltopts,trans=tcp,port=888 127.0.0.1:888 $mtpt
expect /tmp/ns.$USER.:0/foo $dfltopts,trans=unix /tmp/ns.$USER.:0/foo $mtpt
expect /dev/bar $dfltopts,trans=virtio virtio:/dev/bar $mtpt
expect nodev $dfltopts,trans=fd,rfdno=0,wrfdno=1 - $mtpt

dfltopts="trans=tcp,port=123,$dfltopts"

testflag() {
	opts=$1
	shift
	expect 127.0.0.1 $dfltopts,$opts "$@" 127.0.0.1:123 $mtpt
}

testflag "dfltuid=$(id -u),dfltgid=$(id -g)" -i
testflag "access=any" -s
expect 127.0.0.1 $(echo $dfltopts |sed 's/,noextend//') -u 127.0.0.1:123 $mtpt
expect 127.0.0.1 $(echo $dfltopts |sed 's/,nodevmap//') -v 127.0.0.1:123 $mtpt
testflag "access=$(id -u)" -x
testflag "aname=abcdef" -a abcdef
testflag "cache=loose" -c loose
testflag "debug=0x0130" -d fcall,conv,mux
testflag "maxdata=16384" -m 16384

shouldfail() {
	output=$($P9MOUNT -n "$@" 2>&1) && {
		echo '	'9mount "$@"
		echo $output' #should have failed!'
	}
}

shouldfail -a 127.0.0.1:123 $mtpt
shouldfail -a main/active,bwahaha 127.0.0.1:123 $mtpt
shouldfail -d lol 127.0.0.1:123 $mtpt
shouldfail -m z 127.0.0.1:123 $mtpt
