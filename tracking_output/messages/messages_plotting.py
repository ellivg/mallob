from matplotlib import pyplot as plt
from collections import defaultdict
import math
import os

# Assign directory
dir_list = [r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run1",
            r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/all_in_run2"]

# Variables
non_tracking_files = []
values = defaultdict(list)
labels = []
tracking_lines = []
finished_values = defaultdict(list)

# Iterate over files in directory
for directory in dir_list:
    for name in os.listdir(directory):
        tracking_values = []
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
                if ("[tracking]" in line) and (str.casefold(wanted_keyword) in str.casefold(line)):
                    line = line.split("Number of queries in ")[1]
                    tracking_lines.append(line)

# Delete line delimiters and [tracking] keyword
for line in tracking_lines:
    line = line[:-1] # Delete line delimiter
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

    #if int(num_all) >= 100:
    #    continue

    for value in finished_line:
        perc = float(value[1]) / float(num_all)
        finished_values[value[0]].append(perc)

# Print non solved files
if not non_tracking_files:
    print("All files were solved")
for name in non_tracking_files:
    print(name+" was not solved")

x, y = zip(*finished_values.items())
values = list(x), y

print(values[0])
values[0][1] = "not\nallowed:"
values[0][2] = "recipient\ninvalid:"
values[0][3] = "return\nempty:"

plt.boxplot(x=values[1], tick_labels=values[0])

plt.xlim([0, len(values[0])+1])
plt.ylim([0, 1])

plt.ylabel("Number of queries")

plt.savefig("tracking_output/messages/messages_plot_in11.pdf", format="pdf")