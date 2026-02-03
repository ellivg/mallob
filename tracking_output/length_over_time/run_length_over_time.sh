#!/bin/bash

source /nfs/software/setup.sh  #Load spack here on the target machine

if ! spack env list | grep -q mallob_env; then
    echo "Error: Spack environment mallob_env missing, must be created first"
    return 0
fi

spack env activate mallob_env

echo "Build: Fetching SAT solvers from Github"
(cd lib && bash fetch_and_build_solvers.sh kcly)

echo "Build: Cleaning build/ "
mkdir -p build
rm build/*mallob*
cd build

#with USE_JEMALLOC=0, the linker won't find lz, and adding libz results in spack compiling every library like gcc which takes pretty long

CC=$(which mpicc) 
CXX=$(which mpicxx) 
cmake -DMALLOB_JEMALLOC_DIR=/nfs/home/$USER/.user_spack/environments/mallob_env/.spack-env/view/lib \
  -DCMAKE_BUILD_TYPE=RELEASE \
  -DMALLOB_APP_SAT=1 -DMALLOB_APP_BNB=1 \
  -DMALLOB_USE_JEMALLOC=1 \
  -DMALLOB_LOG_VERBOSITY=4 \
  -DMALLOB_ASSERT=1 \
  -DMALLOB_SUBPROC_DISPATCH_PATH=\"build/\" ..

make clean
make -j 20
cd ..

MPI_PROCESSES=4 #TODO: Set to desired number 

echo "" 
echo ""
echo "Using $((MPI_PROCESSES * THREADS_PER_PROCESS))/$(nproc) cores"
echo $(lscpu | grep "Model name")

echo "MPI_PROCESSES: $MPI_PROCESSES"
echo "THREADS_PER_PROCESS: $THREADS_PER_PROCESS"
echo "INSTANCES: $INSTANCES"

OUT_DIR="tracking_output/length_over_time/out/" #TODO: Set to own paths

MALLOB_OPTIONS=" \
  -mono-app=bnb \
  -jcup=0.1 \ 
  -pre-cleanup=1 \
"

echo "MALLOB_OPTIONS"
echo "MPI_PROCESSES: $MPI_PROCESSES"
echo $MALLOB_OPTIONS | tr ' ' '\n'

# create an output dir for each instance
MY_LOG="$OUT_DIR/$i/"
MY_TMP="$OUT_DIR/$i/tmp/"
mkdir -p $MY_LOG
mkdir -p $MY_TMP

MY_MALLOB_OPTIONS="$MALLOB_OPTIONS \
  -mono=instances/bnb/basic/bnb_n25_m2.cnf\
  -log=$MY_LOG \
  -trace-dir=$MY_LOG \
  -tmp=$MY_TMP
"

echo "MY_MALLOB_OPTIONS"
echo $MY_MALLOB_OPTIONS | tr ' ' '\n'
echo "" 
echo ""

for ((j=6; j<=55; j++)); do
  echo "Run Nr $j"
  mpirun -np $MPI_PROCESSES build/mallob $MY_MALLOB_OPTIONS >tracking_output/length_over_time/"file_4_"$j".txt"
done