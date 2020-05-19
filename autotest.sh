#!/bin/sh

usage="usage: autotest.sh [option]
    -h print this help message
    -N use current rbtraced instead of start a new one"

die()
{
    echo 1>&2 $@
    exit -1
}

kill_rbtraced()
{
    _pid=$(pidof rbtraced)
    if [[ -n $_pid && ! -z "$_pid" ]]; then
        kill $_pid
    fi
}

start_rbtraced()
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

rbtrace_config()
{
    ./rbt -S TEST
    if [ $? -ne 0 ]; then
        die "config rbtrace failed"
    fi
}

# open_trace_file <filename>
open_trace_file()
{
    ./rbt -o $1 -w on -s 32
    if [ $? -ne 0 ]; then
        die "rbt open trace file failed"
    fi
    sleep 1
    if [ ! -r $1 ]; then
        die "trace file not generated"
    fi
}

# close_trace_file
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

# parse_trace_file <filename> <nr_traces>
parse_trace_file()
{
    ./prbt -f $1 -o $1.txt
    trace_nr=$(cat $1.txt | egrep "TEST|NULL" | wc -l)
    if [ $trace_nr -ne $2 ]; then
        die "trace record number inconsistent($trace_nr:$2)"
    fi
    rm -f $1.txt
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
	# by default kill old rbtraced and start a new one
	kill_rbtraced
	start_rbtraced
	;;
esac

TEST_ROUND=3
TRACE_FILE_NAME=trace.rbt

rbtrace_config

rm -f $TRACE_FILE_NAME
rm -f $TRACE_FILE_NAME.txt

i=0
while [ $i -lt $TEST_ROUND ]
do
    open_trace_file $TRACE_FILE_NAME

    ./rbt -i
    if [ $? -ne 0 ]; then
        die "rbt info failed"
    fi

    # Start trace benchmark
    ./rbtbench -p 1 -t 1 -n 65538
    if [ $? -ne 0 ]; then
        die "rbtbench failed"
    fi

    # Close and flush trace file
    close_trace_file

    # Parse trace file
    parse_trace_file $TRACE_FILE_NAME 131076

    rm -f "$TRACE_FILE_NAME"
    rm -f "$TRACE_FILE_NAME.txt"

    (( i++ ))
done

open_trace_file $TRACE_FILE_NAME

./test_segfault
echo "test segfault done"

_res=$(file core.* | grep "test_segfault")
echo "$_res"
if [[ -z "$_res" ]]; then
    die "unknown core generated"
fi

rm -f core.*

close_trace_file

./rbt -C TEST
if [ $? -ne 0 ]; then
    die "clear rbtrace failed"
fi

rm -f $TRACE_FILE_NAME*

kill_rbtraced
sleep 1
if [ -e /dev/shm/rbtracebuf ]; then
    die "shared memory *not* cleaned"
fi
if [ -e /dev/shm/sem.rbtrace ]; then
    die "semaphore *not* cleaned"
fi

echo "Autotest passed."
