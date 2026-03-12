from matplotlib import pyplot as plt
from collections import defaultdict
import math
import os
import re

# Assign directory
directory = r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in9"

# Variables
non_tracking_files = []
values = defaultdict(list)
wanted_keyword = "RESPONSE_TIME"

# Iterate over files in directory
for name in os.listdir(directory):
    tracking_lines = []
    tracking_values = []
    finished_values = []
    file_path = os.path.join(directory, name)

    #if re.match(r'file_[0-9]_1_[0-9]\.txt', name):
    #    print(name)
    #    continue

    if not os.path.isfile(file_path):
        continue
    
    with open(file_path) as file:
        #print("Opening: "+name)
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
    
    #print(tracking_lines)
    
    # Delete line delimiters and [tracking] keyword
    for line in tracking_lines:
        line = line[:-1]
        line = line.split(" ")
        line = line[1:3] + line[4:]
        tracking_values.append(line)
    
    #print(tracking_values)

    # Only use the relevant line
    for line in tracking_values:
        finished_values.append(float(line[-3]))

    #print(finished_values)

    # Get label
    label = float(name.split("_")[2])
    #print(label)
    
    # Decide which values are important for the current plot and add them to the dict
    for line in finished_values:
        values[label].append(line)
    
    #print(values)

# Print non solved files
if not non_tracking_files:
    print("All files were solved")
for name in non_tracking_files:
    print(name+" was not solved")

# Get current compare: 2v4, 4v8
key1 = 4
key2 = 8
values1 = sorted(values[key1])
values2 = sorted(values[key2])
#print(values1)
#print(values2)

# specific: TODO changes
print(len(values1))
print(len(values2))
values1 = values1
values2 = values2


plt.plot([0,300], [0,300], color="black")

plt.scatter(values1, values2, color="red")

plt.xlim([0, 100])
plt.ylim([0, 100])

plt.xlabel(str(key1)+" threads")
plt.ylabel(str(key2)+" threads")

fig_name = "tracking_output/1v1/"+str(key1)+"v"+str(key2)+"/out/1v1_"+str(key1)+"v"+str(key2)+"_in9.png"
plt.savefig(fig_name)