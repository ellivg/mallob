set -eu  #Abort if encounter error or unset variable

MPI_PROCESSES=4 #TODO: Set to desired number 
INSTANCES=1 #TODO: Set to desired number (or count paths in paths.txt file)

echo "" 
echo ""
echo "Using $((MPI_PROCESSES))/$(nproc) cores"
echo $(lscpu | grep "Model name")

echo "MPI_PROCESSES: $MPI_PROCESSES"
echo "THREADS_PER_PROCESS: $THREADS_PER_PROCESS"
echo "INSTANCES: $INSTANCES"

OUT_DIR="scripts/bnb/out/" #TODO: Set to own paths
INST_PATHS_TXT="scripts/bnb/in/paths.txt" #TODO: Set to own instances

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
  -pre-cleanup=1
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
    -mono=$INST_PATH \
    -log=$MY_LOG \
    -trace-dir=$MY_LOG \
    -tmp=$MY_TMP
  "

  echo "MY_MALLOB_OPTIONS"
  echo $MY_MALLOB_OPTIONS | tr ' ' '\n'
  echo "" 
  echo ""

  mpirun -np $MPI_PROCESSES --bind-to core build/mallob $MY_MALLOB_OPTIONS 
done 

echo ""
echo "Successfully processed $INSTANCES instances"


