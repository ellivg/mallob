from matplotlib import pyplot as plt
from collections import defaultdict
import math
import os

# Assign directory
dir_list = [r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run1",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run2",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run3",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/scripts/bnb/out"]

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

        # Get label (Nr of Threads)
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

# Convert dict to two lists [labels, [numbers]]
#print(values)
myList = sorted(values.items())
x, y = zip(*myList)
values = list(map(int, x)), list(sorted(y_list) for y_list in y)
#print(values)

linestyle_str = ['solid', 'dotted', 'dashed', 'dashdot'] 

# Plot by label
for i in range(0, len(values[0])):
    linestyle_count = i % len(linestyle_str)
    plt.plot(values[1][i], list(range(0,len(values[1][i]))), label=values[0][i], linestyle=linestyle_str[linestyle_count])

plt.xscale('log')
#plt.xlim([0, 300]) # max should be 300secs
plt.ylim([0, 120]) # nr of examples times 10

plt.ylabel("# of instances solved")
plt.xlabel("run time [s]")
plt.legend()

fig_name = "tracking_output/cdf/cdf_plot13_log.pdf"
plt.savefig(fig_name, format="pdf")