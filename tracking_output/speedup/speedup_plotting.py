from matplotlib import pyplot as plt
from collections import defaultdict
import numpy as np
import math
import os

# Assign directory
dir_list = [r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/speedup/in10",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/speedup/in15",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/speedup/in20",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/speedup/in25_1",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/speedup/in25_2",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/speedup/in30"]

# Variables
non_tracking_files = []
all_values = []
wanted_keyword = "RESPONSE_TIME"
num_files = 0

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
        label = float(name.split("_")[2])
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

    for i in range(0,2):
        sum = np.log(values[1][i])
        gmean = np.exp(sum.mean())
        values[1][i] = gmean
    
    speedup = values[1][0] / values[1][1]
    all_values.append((values[1][1], speedup))


# Print non solved files
if not non_tracking_files:
    print("All files were solved")
percent = len(non_tracking_files) / num_files
print(str(percent)+"% was not solved")

#print(values)
x, y = zip(*all_values)

plt.plot([0,300], [1,1], color="black")
plt.plot(x, y, color="red")

plt.xlim([0, 150])
plt.ylim([0, 2])

plt.ylabel("Speedup")
plt.xlabel("T_parallel")

fig_name = "tracking_output/speedup/speedup.png"
plt.savefig(fig_name)
