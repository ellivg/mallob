from matplotlib import pyplot as plt
from collections import defaultdict
import math
import os

# Assign directory
dir_list = [r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/1v1/2v4/in10",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/1v1/2v4/in20",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/1v1/2v4/in30"]

# Variables
non_tracking_files = []
values = defaultdict(list)
wanted_keyword = "RESPONSE_TIME"

# Iterate over files in directory
for directory in dir_list:
    for name in os.listdir(directory):
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

# Print non solved files
if not non_tracking_files:
    print("All files were solved")
for name in non_tracking_files:
    print(name+" was not solved")

#print(values)
myList = sorted(values.items())
x, y = zip(*myList)
values = list(map(int, x)),  y
print(values)

# specific: TODO changes
print(len(values[1][0]))
print(len(values[1][1]))
values_two = sorted(values[1][0])
values_four = sorted(values[1][1][:-1])


plt.plot([0,300], [0,300], color="black")

plt.scatter(values_two, values_four, color="red")

plt.xlim([0, 150])
plt.ylim([0, 150])

plt.ylabel("4 threads")
plt.xlabel("2 threads")

fig_name = "tracking_output/1v1/2v4/out/1v1_2v4.png"
plt.savefig(fig_name)