# rbtrace
rbtrace is a ring buffer based tracing system running on X86 linux platform.
It's very effective because we use lockfree algorithms.
Currently we only support multi-process/multi-thread fixed size tracing which is useful for performance/behavior analyzing.

## Building

```
$ make all
```

## usage

You need to start the rbtrace daemon first

### start rbtraced

```
$ ./rbtraced -d
```

Then open a trace file for tracing

### open trace file

```
$ ./rbt -o trace.dat
```

Set the trace to be printed

### set trace 

```
$ ./rbt -S TEST
```

Start a program to print the traces, e.g. rbtbench.

### print trace

```
$ ./rbt_bench -p 2 -t 2 -n 10000
```

Then close and flush the trace file

### close trace file

```
$ ./rbt -c
```

Finally, parse the content in trace file

### print trace file

```
$ ./prbt -f trace.dat
```