#!/bin/sh

usage="usage: autotest.sh [option]
    -h print this help message
    -N use current otraced instead of start a new one"

die()
{
    echo 1>&2 $@
    exit -1
}

kill_otraced()
{
    _pid=$(pidof rbtraced)
    if [[ -n $_pid && ! -z "$_pid" ]]; then
        kill $_pid
    fi
}

start_otraced()
{
    ./rbtraced -d
    # sleep a while for rbtraced to get ready
    sleep 2
    _pid=$(pidof rbtraced)
    ./rbt -i
    if [ $? -ne 0 ]; then
        die "rbtraced is not ready yet"
    fi
}

close_trace_file()
{
    ./rbt -c
    if [ $? -ne 0 ]; then
	echo "rbt close failed"
    else
        # sleep a while for traces to be flushed
        sleep 4
    fi
}

ulimit -c unlimited

case "$1" in
    "-h")
	echo "$usage"
	;;
    "-N")
	# close trace file if there is any
	close_trace_file
	;;
    *)
	# by default kill old otraced and start a new one
	kill_otraced
	start_otraced
	;;
esac


rm -f trace.dat*
rm -f trace.txt
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

./rbtbench -p 1 -t 1 -n 65536
if [ $? -ne 0 ]; then
    die "rbtbench failed"
fi

# Close and flush trace file
close_trace_file

_trace_file=$(ls trace.dat.*)
./prbt -f $_trace_file -o trace.txt
_trace_num=$(cat trace.txt | egrep "TEST|LOST" | awk '{print $7}' | wc -l)
# rbtbench will record start and done in trace file for 1 op
if [ $_trace_num -ne 131072 ]; then
    die "trace record number inconsistent"
fi

./rbt -o trace.dat -z on -s 20
if [ $? -ne 0 ]; then
    die "rbt open trace file failed"
fi

./test_segfault
echo "test segfault done"

_res=$(file core.* | grep "test_segfault")
echo "$_res"
if [[ -z "$_res" ]]; then
    die "unknown core generated"
fi

rm -f core.*

close_trace_file

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
