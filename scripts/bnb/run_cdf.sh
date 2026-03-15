#!/bin/bash

THREADS_PER_PROCESS=1 # Set to desired number
INSTANCES=1 #TODO: Set to desired number (or count paths in paths.txt file)

echo "" 
echo ""
echo $(lscpu | grep "Model name")
echo "Running CDF"

echo "MPI_PROCESSES: variable" # TODO: change if applicable
echo "THREADS_PER_PROCESS: $THREADS_PER_PROCESS"
echo "INSTANCES: $INSTANCES"

OUT_DIR="scripts/bnb/out/"
INST_PATHS_TXT="scripts/bnb/in/paths.txt"

(cd scripts/server/example_in; find "$(pwd)" -type f -name "*.xz" > paths.txt) #TODO remove. We create paths.txt this way only here for the example to have valid full paths

#Clean old logs and traces
: "${OUT_DIR:?ERROR: OUT_DIR is not set or empty}"  #safety measure to not accidentaly rm -rf the whole /* (!!)
mkdir -p "$OUT_DIR"
rm -rf "$OUT_DIR"/*

if [[ ! -f "$INST_PATHS_TXT" ]]; then
    echo "File '$INST_PATHS_TXT' does not exist"
    exit 1
fi

MALLOB_OPTIONS=" \
  -mono-app=bnb \
  -jcup=0.1 \ 
  -pre-cleanup=1 \
"

echo "MALLOB_OPTIONS"
echo $MALLOB_OPTIONS | tr ' ' '\n'

# main loop over instances
INSTANCES_PROCESSED=0
for ((i=1; i<=INSTANCES; i++)); do
  echo "" 
  echo ""
  echo "Reading path of instance Nr $i"
  INST_PATH=$(cat $INST_PATHS_TXT|sed $i'q;d')

  [[ -z "$INST_PATH" ]] && continue #check for empty line

  echo "Processing instance Nr. $i: ($INST_PATH)"

  # create an output dir for each instance
  MY_LOG="$OUT_DIR/$i/"
  MY_TMP="$OUT_DIR/$i/tmp/"
  mkdir -p $MY_LOG
  mkdir -p $MY_TMP

  MY_MALLOB_OPTIONS="$MALLOB_OPTIONS \
    -T=300 \
    -mono=$INST_PATH \
    -log=$MY_LOG \
    -trace-dir=$MY_LOG \
    -tmp=$MY_TMP
  "

  echo "MY_MALLOB_OPTIONS"
  echo $MY_MALLOB_OPTIONS | tr ' ' '\n'
  echo "" 
  echo ""

  for ((j=0; j<=5; j++)); do
    ((MPI_PROCESSES=2**$j)) #TODO: Set to desired number 

    echo "MPI_PROCESSES: $MPI_PROCESSES"

    for ((k=0; k<=9; k++)); do
      echo "Run Nr $k"
      mpirun -np $MPI_PROCESSES --bind-to core --map-by :OVERSUBSCRIBE  build/mallob $MY_MALLOB_OPTIONS >scripts/bnb/out/"file_"$i"_"$MPI_PROCESSES"_"$k".txt"
    done
  done
done 

echo ""
echo "Successfully processed $INSTANCES instances"