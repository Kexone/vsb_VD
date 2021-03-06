set terminal pdfcairo enhanced color font "sans,6" fontscale 1.0 linewidth 1 rounded dashlength 1 background "white" size 10cm,6cm 
set encoding utf8
set datafile separator ","
set output "cv_02.pdf"

#definovani barvy os
set style line 80 linetype 1 linecolor "#808080" linewidth 2 
set border 3 back linestyle 80

#mrizka
set style line 81 lt 0
set style line 81 lt rgb "0x00D3D3D3"
set style line 81 lw 0.5
set grid back linestyle 81

set xtics nomirror
set ytics nomirror


set title "Average temperatures in Dublin (1955-2017)" font "sans-Bold"
set xlabel "f [Hz]"
set ylabel "Amplitude"

#set key bottom left Left title "Probes:" enhanced font "sans-Italic" #reverse box

set xrange [0.0:0.5]
set yrange [-0.1:0.4]

set style line 1 lt rgb "#A00000" lw 2 pt 1


plot "dublin_temps_frequency.csv" using 1:2 title "Temps" with lines