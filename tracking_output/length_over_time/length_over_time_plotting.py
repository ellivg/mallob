from matplotlib import pyplot as plt
from collections import defaultdict
import math
import os

# Assign directory
directory = r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/length_over_time/out"

# Variables
non_tracking_files = []
values = defaultdict(list)

# Iterate over files in directory
for name in os.listdir(directory):
    tracking_lines = []
    tracking_values = []
    finished_values = []
    wanted_keyword = "solved"
    file_path = os.path.join(directory, name)

    if not os.path.isfile(file_path):
        continue
    
    with open(file_path) as file:
        # print("Opening: "+name)
        # Delete non-solved files
        if not "[solved]" in file.read():
            non_tracking_files.append(name)
            continue

        file.seek(0)

        # Add all lines relevant for tracking
        for line in file:
            line.strip()
            if wanted_keyword in line:
                tracking_lines.append(line)
    
    # print(tracking_lines)
    
    # Delete line delimiters and [tracking] keyword
    for line in tracking_lines:
        line = line[:-1]
        line = line.split(" ")
        line = line[1:3] + line[4:]
        tracking_values.append(line)
    
    # print(tracking_values)

    # Probably later prettier but in basic the only necessary numbers are the timestamp line[0] and percentage line[-1]
    for line in tracking_values:
        finished_values.append(float(line[0]))

    # print(finished_values)
    
    # Decide which values are important for the current plot and add them to the dict
    label = name.split("_")[1]
    for line in finished_values:
        values[label].append(line)
    
    print(values)

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

plt.boxplot(x=values[1], tick_labels=values[0])

plt.xlim([0, len(values[0])+1])
plt.ylim([0, max_value])

plt.title("Compare if changing the number of threads \n does anything on a small example")
plt.ylabel("Time")
plt.xlabel("Number Of Threads")

max_value = math.ceil(max(max(sub_list) for sub_list in sorted_values))
min_value = math.floor(min(min(sub_list) for sub_list in sorted_values))

plt.boxplot(x=sorted_values, tick_labels=[10,15,20,25])
plt.ylim([0, 100])
plt.xlim([0, 5])

plt.title("Compare computing time for length of input")
plt.ylabel("Time")
plt.xlabel("Input Length n")

plt.savefig("length_over_time/length_over_time_plot.png")