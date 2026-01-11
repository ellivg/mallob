#! /bin/sh

files=("bnb_n10_m2.cnf" "bnb_n15_m2.cnf")

echo "Start computing"
for file in ${files[*]}
do
    echo "Now executing $file"

    for i in {1..1}
    do
        echo "Run $i"
        mpirun -np 2 build/mallob -mono="instances/bnb/$file" -mono-app=bnb -jcup=0.1 -pre-cleanup=1 >tracking_output/basic/files/"$file"_"$i"
    done
done
echo "End computing"

echo "Start evaluating"
python -u "/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/basic_plotting.py"
echo "End evaluating"