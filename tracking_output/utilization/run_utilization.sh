#!/bin/bash

sh scripts/setup/build.sh

MPI_PROCESSES=4 #TODO: Set to desired number 

MALLOB_OPTIONS=" \
  -mono-app=bnb \
  -jcup=0.1 \ 
  -pre-cleanup=1 \
"

MY_MALLOB_OPTIONS="$MALLOB_OPTIONS \
  -mono=instances/bnb/basic/bnb_n20_m2.cnf\
"

for ((j=1; j<=9; j++)); do
  echo "Run Nr $j"
  mpirun -np $MPI_PROCESSES build/mallob $MY_MALLOB_OPTIONS >tracking_output/utilization/out_new/"log_"$j".txt"
done

python -u "/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/utilization/utilization_plotting.py"