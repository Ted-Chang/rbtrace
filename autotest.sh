#!/bin/sh

die()
{
    echo 1>&2 $@
    exit -1
}

_pid=$(pidof rbtraced)
if [ -n $_pid ]; then
    kill $_pid
fi

ulimit -c unlimited

_pid=$(./rbtraced -d)
./rbt -i
if [ $? -ne 0 ]; then
    die "rbtraced is not ready yet"
fi

rm -f trace.dat*
./rbt -o trace.dat -z on -s 20
if [ $? -ne 0 ]; then
    die "rbt open trace file failed"
fi

./rbtbench -p 2 -t 2 -n 1000000
if [ $? -ne 0 ]; then
    die "rbtbench failed"
fi

./rbt -c
if [ $? -ne 0 ]; then
    die "rbt close trace file failed"
fi

kill $_pid
