from matplotlib import pyplot as plt
from collections import defaultdict
import math
import os

# Assign directory
directory = r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/utilization/out"

# Variables
non_tracking_files = []
values = defaultdict(list)
labels = []

# Iterate over files in directory
for name in os.listdir(directory):
    tracking_lines = []
    tracking_values = []
    finished_values = []
    wanted_keyword = "queries"
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
            if "[tracking]" in line and str.casefold(wanted_keyword) in str.casefold(line):
                tracking_lines.append(line)
    
    # Delete line delimiters and [tracking] keyword
    for line in tracking_lines:
        line = line[:-1]
        line = line.split("-")

        finished_line = []
        num_all = -1
        for value in line:
            value = value.split(" ")
            if value[-1] == "":
                value = value[:-1]
            value = value[-2:]

            if value[0] == "total:":
                num_all = value[1]
            else:
                finished_line.append(value)
            
        for value in finished_line:
            print(value)
            perc = float(value[1]) / float(num_all)
            finished_values.append((value[0],perc))
        

    # Decide which values are important for the current plot and add them to the dict
    for line in finished_values:
        if line[0] not in labels:
            labels.append(line[0])
        values[labels.index(line[0])].append(line[1])
    

# Print non solved files
if not non_tracking_files:
    print("All files were solved")
for name in non_tracking_files:
    print(name+" was not solved")


print(labels)
print(values)
myList = sorted(values.items())
x, y = zip(*myList)
values = list(map(int, x)), y
print(values)

max_value = math.ceil(max(max(sub_list) for sub_list in values[1]))
min_value = math.floor(min(min(sub_list) for sub_list in values[1]))

plt.boxplot(x=values[1], tick_labels=labels)

plt.xlim([0, len(values[0])+1])
plt.ylim([min_value, max_value])

plt.title("Percentage of all queries")
plt.xlabel("Type of return")
plt.ylabel("Number of queries")

plt.savefig("tracking_output/messages/messages_plot_new.png")