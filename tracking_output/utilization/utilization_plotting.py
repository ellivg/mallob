from matplotlib import pyplot as plt
from collections import defaultdict
import math
import os

# Assign directory
directory = r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/utilization/test"

# Variables
non_tracking_files = []
values = defaultdict(list)

# Iterate over files in directory
for name in os.listdir(directory):
    tracking_lines = []
    tracking_values = []
    finished_values = []
    wanted_keyword = "Time"
    file_path = os.path.join(directory, name)

    if not os.path.isfile(file_path):
        continue
    
    with open(file_path) as file:
        # Delete non-solved files
        if not "[solved]" in file.read():
            non_tracking_files.append(name)
            continue

        file.seek(0)

        # Add all lines relevant for tracking
        for line in file:
            line.strip()
            if "[tracking]"  in line:
                tracking_lines.append(line)
    
    # Delete line delimiters and [tracking] keyword
    for line in tracking_lines:
        if str.casefold(wanted_keyword) in str.casefold(line):
            line = line[:-1]
            line = line.split(" ")
            line = line[1:3] + line[4:]
            tracking_values.append(line)
            # print(line)

    # Probably later prettier but in basic the only necessary numbers are the timestamp line[0] and percentage line[-1]
    for line in tracking_values:
        finished_values.append((int(line[-2]),float(line[-1])))

    # Decide which values are important for the current plot and add them to the dict
    for line in finished_values:
        values[line[0]].append(line[1])

# Print non solved files
if not non_tracking_files:
    print("All files were solved")
for name in non_tracking_files:
    print(name+" was not solved")

print(values)
myList = sorted(values.items())
x, y = zip(*myList)
values = list(map(int, x)), y
print(values)

max_value = math.ceil(max(max(sub_list) for sub_list in values[1]))
min_value = math.floor(min(min(sub_list) for sub_list in values[1]))

plt.boxplot(x=values[1], tick_labels=["wait\nthreads", "check", "wait\nfull", "get\nwork", "work", "compare", "outside\n"])

plt.xlim([0, len(values[0])+1])
plt.ylim([0, 1])

plt.title("Auslastung bei einzigem Unterschied in der Job-Anzahl \n bei gleicher Maschinen- und Threadanzahl (2)")
plt.xlabel("")
plt.ylabel("Auslastung")

plt.savefig("tracking_output/utilization/utilization_plot_30_new.png")