#!/usr/bin/gnuplot --persist
#
# plot iostat of a single disk
#
# usage: gnuplot -e "plottitle='sda IO stats'; plotdata='sda.out'; plotout='sda.png'" rbtplot.gp
#

set terminal pngcairo size 960,540 enhanced font 'Verdana,10'
set output plotout
set title plottitle

# line definition
set style line 1 lc rgb "#E41A1C" lw 1 lt 1;
set style line 2 lc rgb "#377EB8" lw 1 lt 1;
set style line 3 lc rgb "#4DAF4A" lw 1 lt 1;
set style line 4 lc rgb "#984EA3" lw 1 lt 1;
set style line 5 lc rgb "#FF7F00" lw 1 lt 1;
set style line 6 lc rgb "#DADA33" lw 1 lt 1;
set style line 7 lc rgb "#A65628" lw 1 lt 1;
set style line 20 lw 1 lt 0

set grid ls 20

# key position
set key top left

# Enable the use of macros
set macros
# MACROS
GRAPH1="set tmargin at screen 0.90; set bmargin at screen 0.62; set lmargin at screen 0.07"
GRAPH2="set tmargin at screen 0.58; set bmargin at screen 0.32; set lmargin at screen 0.07"
GRAPH3="set tmargin at screen 0.28; set bmargin at screen 0.15; set lmargin at screen 0.07"

### Start multiplot (3 x 1 layout)
set multiplot layout 3,1 rowsfirst

# --- GRAPH bandwidth
@GRAPH1
unset xlabel
unset ylabel
# hide xtic labels
set format x ""
plot plotdata using 1 title "read(MB/s)" with lines, \
     plotdata using 2 title "write(MB/s)" with lines, \
     plotdata using 3 title "total(MB/s)" with lines

# --- GRAPH IOPS
@GRAPH2
unset title
unset xlabel
unset ylabel
# hide xtic labels
set format x ""
plot plotdata using 4 title "read/s" with lines, \
     plotdata using 5 title "write/s" with lines, \
     plotdata using 6 title "IOPS" with lines

# --- GRAPH %util
@GRAPH3
unset title
set xlabel "Time (sec)"
unset ylabel
# restore xtic labels
set format x
set ytics ('0' 0.00, '25' 25.00, '50' 50.00, '75' 75.00, '100' 100.00)
set yrange [0:100.00]
plot plotdata using 7 title "disk %util" with lines

### End multiplot
unset multiplot

