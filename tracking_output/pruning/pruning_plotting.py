from matplotlib import pyplot as plt
from collections import defaultdict
import numpy as np
import math
import os

# Assign directory
dir_list = [r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in9",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in10_pruning"]

# Variables
non_tracking_files = []
values_noprune = {}
values_prune = {}
wanted_keyword = "RESPONSE_TIME"

# Iterate over files in directory
for directory in dir_list:
    #print(directory)
    values = {}
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
        label_thr = int(name.split("_")[2])
        label_file = int(name.split("_")[1])
        #print(str(label_thr)+" "+str(label_file))
        
        # Decide which values are important for the current plot and add them to the dict
        for line in finished_values:
            if (not bool(values)) or (not label_thr in values):
                values[label_thr] = {}
            if (not bool(values[label_thr])) or (not label_file in values[label_thr]):
                values[label_thr][label_file] = []

            values[label_thr][label_file].append(line)
        
        #print(values)
    if not bool(values_prune):
        values_prune = values
    else:
        values_noprune = values
    
    #print(values_prune)
    #print(values_noprune)

# Print non solved files
if not non_tracking_files:
    print("All files were solved")
for name in sorted(non_tracking_files):
    print(name+" was not solved")

# Compute T_noprune
for ikey, ivalue in values_noprune.items():
    for jkey, jvalue in values_noprune[ikey].items():
        sum = np.log(values_noprune[ikey][jkey])
        gmean = np.exp(sum.mean())
        values_noprune[ikey][jkey] = gmean

# Compute T_prune
for ikey, ivalue in values_prune.items():
    for jkey, jvalue in values_prune[ikey].items():
        sum = np.log(values_prune[ikey][jkey])
        gmean = np.exp(sum.mean())
        values_prune[ikey][jkey] = gmean

# Compute speedup
pop = []
for ikey, ivalue in values_prune.items():
    for jkey, jvalue in values_prune[ikey].items():
        if not jkey in values_noprune[ikey]:
            pop.append((ikey, jkey))
        else:
            speedup = values_noprune[ikey][jkey] / values_prune[ikey][jkey]
            values_prune[ikey][jkey] = speedup
for pop_item in pop:
    values_prune[pop_item[0]].pop(pop_item[1])

marker_rotation = ["o", "v", "s", "p", "*", "D"]
marker_count = 0

# Plot
for key, value in values_prune.items():
    sorted_keys = sorted(values_noprune[key], key=values_noprune[key].get)
    d1_sorted = {k: values_noprune[key][k] for k in sorted_keys}
    d2_sorted = {k: values_prune[key][k] for k in sorted_keys}

    x, y = zip(*d2_sorted.items())
    value_prune = x,y

    x, y = zip(*d1_sorted.items())
    value_noprune = x,y

    print(key)
    print(value_prune)
    print(value_noprune)

    plt.scatter(value_noprune[1], value_prune[1], label=key, marker=marker_rotation[marker_count])

    marker_count += 1
    marker_count %= len(marker_rotation)


plt.plot([0,300], [1,1], color="black")

plt.xlim([0, 300])
plt.ylim([0, 3])

plt.xlabel("time with no pruning")
plt.ylabel("speedup")

plt.legend()

fig_name = "tracking_output/pruning/pruning_in9.pdf"
plt.savefig(fig_name, format="pdf")
