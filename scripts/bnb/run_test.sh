set -eu  #Abort if encounter error or unset variable

mpirun -np 2 build/mallob  -mono=scripts/bnb/in/bnb_n20_m2.cnf -mono-app=bnb -jcup=0.1 -pre-cleanup=1


