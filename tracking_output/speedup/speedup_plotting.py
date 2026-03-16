from matplotlib import pyplot as plt
from collections import defaultdict
import numpy as np
import math
import os

# use cdf runs as base

# Assign directory
dir_list = [r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run1",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run2",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run2"]

# Variables
non_tracking_files = []
values = {}
wanted_keyword = "RESPONSE_TIME"
file_nr = 1

# Iterate over files in directory
for directory in dir_list:
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
        label_thr = int(name.split("_")[2])
        label_file = int(name.split("_")[1])
        
        # Decide which values are important for the current plot and add them to the dict
        for line in finished_values:
            if (not bool(values)) or (not label_thr in values):
                values[label_thr] = {}
            if (not bool(values[label_thr])) or (not label_file in values[label_thr]):
                values[label_thr][label_file] = []

            values[label_thr][label_file].append(line)
        
        #print(values)
        file_nr += 1

# Print non solved files
if not non_tracking_files:
    print("All files were solved")
for name in sorted(non_tracking_files):
    print(name+" was not solved")

# Get T_sequential
for key, value in values[1].items():
    sum = np.log(values[1][key])
    gmean = np.exp(sum.mean())
    values[1][key] = gmean

# Compute speedup
for ikey, ivalue in values.items():
    if ikey == 1:
        continue

    # Necesssary rn TODO change
    values[ikey].pop(7)

    for jkey, jvalue in values[ikey].items():
        sum = np.log(values[ikey][jkey])
        gmean = np.exp(sum.mean())
        speedup = values[1][jkey] / gmean
        values[ikey][jkey] = speedup

marker_rotation = ["o", "v", "s", "p", "*", "D"]
marker_count = 0

# Plot
for key, value in values.items():
    if key == 1:
        continue

    sorted_keys = sorted(values[1], key=values[1].get)
    d1_sorted = {k: values[1][k] for k in sorted_keys}
    d2_sorted = {k: values[key][k] for k in sorted_keys}

    x, y = zip(*d2_sorted.items())
    value_spd = x,y

    x, y = zip(*d1_sorted.items())
    value_seq = x,y

    print(key)
    print(value_spd)
    print(value_seq)

    plt.scatter(value_seq[1], value_spd[1], label=key, marker=marker_rotation[marker_count])

    marker_count += 1
    marker_count %= len(marker_rotation)


plt.plot([0,300], [1,1], color="black")

plt.xscale('log')
#plt.xlim([0, 300])
plt.ylim([0, 30])

plt.xlabel("T_sequential")
plt.ylabel("Speedup")

plt.legend()

fig_name = "tracking_output/speedup/out/speedup_in12.pdf"
plt.savefig(fig_name, format="pdf")
