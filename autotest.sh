#!/bin/sh

die()
{
    echo 1>&2 $@
    exit -1
}

_pid=$(pidof rbtraced)
if [[ -n $_pid && ! -z "$_pid" ]]; then
    kill $_pid
fi

ulimit -c unlimited

./rbtraced -d
# sleep a while for rbtraced to get ready
sleep 2
_pid=$(pidof rbtraced)
./rbt -i
if [ $? -ne 0 ]; then
    die "rbtraced is not ready yet"
fi

rm -f trace.dat*
./rbt -o trace.dat -z on -s 20
if [ $? -ne 0 ]; then
    die "rbt open trace file failed"
fi

./rbt -S TEST
if [ $? -ne 0 ]; then
    die "rbt set trace ID failed"
fi

./rbt -i
if [ $? -ne 0 ]; then
    die "rbt info failed"
fi

./rbtbench -p 2 -t 2 -n 1000000
if [ $? -ne 0 ]; then
    die "rbtbench failed"
fi

./test_segfault
echo "test segfault done"

_res=$(file core.* | grep "test_segfault")
echo "$_res"
if [[ -z "$_res" ]]; then
    die "unknown core generated"
fi

rm -f core.*

./rbt -c
if [ $? -ne 0 ]; then
    die "rbt close trace file failed"
fi

kill $_pid
_pid=$(pidof rbtraced)
if [[ -n $_pid && ! -z "$_pid" ]]; then
    die "kill rbtraced failed"
fi
if [ -e /dev/shm/rbtracebuf ]; then
    die "shared memory *not* cleaned"
fi
if [ -e /dev/shm/sem.rbtrace ]; then
    die "semaphore *not* cleaned"
fi

rm -f trace.dat*

echo "Autotest passed."
