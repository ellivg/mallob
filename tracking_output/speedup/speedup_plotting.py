from matplotlib import pyplot as plt
from collections import defaultdict
import numpy as np
import math
import os

# Assign directory
dir_list = [r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/speedup/in1",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/speedup/in2",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/speedup/in4"]

# Variables
non_tracking_files = []
all_values = defaultdict()
wanted_keyword = "RESPONSE_TIME"
num_files = 0
labels = [1,2,4]
labelcou = 0

# Iterate over files in directory
for directory in dir_list:
    values = defaultdict(list)
    

    for name in os.listdir(directory):
        num_files += 1
        tracking_lines = []
        tracking_values = []
        finished_values = []
        file_path = os.path.join(directory, name)

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
        label = float(name.split("_")[1])
        #print(label)
        
        # Decide which values are important for the current plot and add them to the dict
        for line in finished_values:
            values[label].append(line)
        
        #print(values)

    #print(values)
    myList = sorted(values.items())
    x, y = zip(*myList)
    values = list(map(int, x)),  list(y)
    #print(values)

    for i in range(len(values[0])):
        sum = np.log(values[1][i])
        gmean = np.exp(sum.mean())
        values[1][i] = gmean
    
    if not labelcou == 0:
        for i in range(len(values[1])):
            values[1][i] = all_values[1][1][i] / values[1][i]

    all_values[labels[labelcou]] = values
    labelcou += 1


myList = sorted(all_values.items())
x, y = zip(*myList)
all_values = list(x),  list(y)
print(all_values)

# Print non solved files
if not non_tracking_files:
    print("All files were solved")
percent = len(non_tracking_files) / num_files
print(str(percent)+"% was not solved")

plt.plot([0,300], [1,1], color="black")

for i in range(1, len(all_values[0])):
    plt.plot(all_values[1][0][1], all_values[1][i][1], label=all_values[0][i])

plt.xlim([0, 150])
plt.ylim([0, 2])

plt.ylabel("Speedup")
plt.xlabel("T_sequential")

plt.legend()

fig_name = "tracking_output/speedup/speedup.png"
plt.savefig(fig_name)
