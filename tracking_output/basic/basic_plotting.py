from matplotlib import pyplot as plt
from collections import defaultdict
import math
import os

#
# Using: n10;15;20;25;30 m2 thr4
#

# Assign directory
directory = r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/basic/in30/"

# Variables
non_tracking_files = []
values = []

# Iterate over files in directory
for name in os.listdir(directory):
    tracking_lines = []
    tracking_values = []
    finished_values = []
    wanted_keyword = "Time work"
    file_path = os.path.join(directory, name)

    if not os.path.isfile(file_path):
        continue
    
    with open(file_path) as file:
        #print(file_path)

        # Delete non-solved files
        if not "[solved]" in file.read():
            non_tracking_files.append(name)
            continue

        file.seek(0)

        # Add all lines relevant for tracking
        for line in file:
            line.strip()
            if "[tracking]" and wanted_keyword in line:
                tracking_lines.append(line)

        # Delete line delimiters and [tracking] keyword
        for line in tracking_lines:
            line = line[:-1]
            line = line.split(" ")
            tracking_values.append(line)

        # Probably later prettier but in basic the only necessary numbers are the timestamp line[0] and percentage line[-1]
        for line in tracking_values:
            values.append((float(line[1]), float(line[-1])))
    

# Print non solved files
if not non_tracking_files:
    print("All files were solved")
for name in non_tracking_files:
    print(name+" was not solved")

values.sort()
print(values)
x, y = zip(*values)
values = x, y
#print(values)

plt.plot(values[0], values[1])

plt.xlim([0, 150])
plt.ylim([0,1])

plt.title("Auslastung")
plt.xlabel("time")
plt.ylabel("utilization")

plt.savefig("tracking_output/basic/out/basic_new30.png")