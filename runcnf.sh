#! /bin/sh

sh scripts/setup/build.sh

#for file in instances/bnb/cnf/*
#do
#    echo "Now executing $file"
#    mpirun -np 8 build/mallob -mono="$file" -mono-app=bnb -jcup=0.1 -pre-cleanup=1 -DCMAKE_BUILD_TYPE=DEBUG >tracking_output/cnf/"$file"
#done

file=instances/bnb/bnb_n50_m4.cnf
echo "Now executing $file"
mpirun -np 8 build/mallob -mono="$file" -mono-app=bnb -jcup=0.1 -pre-cleanup=1 >tracking_output/cnf/"$file"
echo "End"