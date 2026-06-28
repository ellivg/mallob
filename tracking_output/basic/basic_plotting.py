from matplotlib import pyplot as plt
from collections import defaultdict
import math
import os

# Assign directory
dir_list = [r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run1",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run2",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run3"]

# Variables
non_tracking_files = []
values = defaultdict(list)
num_files = 0

# Iterate over files in directory
for directory in dir_list:
    for name in os.listdir(directory):
        num_files += 1
        tracking_lines = []
        tracking_values = []
        finished_values = []
        wanted_keyword = "Time work"
        file_path = os.path.join(directory, name)

        if not os.path.isfile(file_path):
            continue
        
        with open(file_path) as file:
            print(file_path)

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
            
            thrcount = int(name.split("_")[2])

            # Probably later prettier but in basic the only necessary numbers are the timestamp line[0] and percentage line[-1]
            for line in tracking_values:
                #print(line)
                values[thrcount].append((float(line[1]), float(line[-1])))
    

# Print non solved files
if not non_tracking_files:
    print("All files were solved")
percent = len(non_tracking_files) / num_files
print(str(percent)+"% was not solved")

x, y = zip(*sorted(values.items()))
values = x, y
#values.sort()
print(values)

marker_rotation = ["o", "v", "s", "p", "*", "D"]

# Plot by label
# X is runtime
# Y is utilization
for i in range(0, len(values[0])):
    x, y = zip(*sorted(values[1][i]))
    plt.scatter(x, y, label=values[0][i], marker=marker_rotation[i])

plt.xlim([0, 300])
plt.ylim([0, 1])

plt.xlabel("run time [s]",  fontsize=14)
plt.ylabel("utilization percentage",  fontsize=14)
plt.legend()

plt.savefig("tracking_output/basic/basic_thr_in12_big.pdf", format="pdf")