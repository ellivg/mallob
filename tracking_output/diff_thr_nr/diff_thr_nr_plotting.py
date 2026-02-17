from matplotlib import pyplot as plt
from collections import defaultdict
import math
import os

# Assign directory
dir_list = [r"/home/eliane/Documents/Bachelorarbeit/MY MALLOB/mallob/tracking_output/diff_thr_nr/n20/in"]

def get_values(wanted_keyword=str, index=int, median=bool, ylabel=str, fig=str):
    # Variables
    non_tracking_files = []
    values = defaultdict(list)

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

            # Only use the relevant line (usually index=0 or index=-1)
            for line in tracking_values:
                finished_values.append(float(line[index]))

            #print(finished_values)

            # Get label
            label = float(name.split("_")[2])
            #print(label)

            if median:
                compute = finished_values
                finished_values = []

                number = sum(compute) / label
                finished_values.append(number)

                #print(number)
                
            
            # Decide which values are important for the current plot and add them to the dict
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
    plt.ylim([0, 400000])

    plt.title("Compare if changing the number of threads \n does anything on a small example")
    plt.ylabel(ylabel)
    plt.xlabel("Number Of Threads")

    fig_name = "tracking_output/diff_thr_nr/n20/diff_thr_nr_n20_"+fig+"_spanne.png"
    plt.savefig(fig_name)

def time():
    print("Run Time")

    wanted_keyword = "solved"
    index = 0
    median = False
    ylabel = "Time"
    fig = "time"

    get_values(wanted_keyword, index, median, ylabel, fig)
    

def expl():
    print("Run Explored Nodes")
    
    wanted_keyword = "explored nodes"
    index = -1
    median = False
    ylabel = "Median Explored Nodes"
    fig = "expl"
    
    get_values(wanted_keyword, index, median, ylabel, fig)

def perc():
    print("Run Percentage")

    wanted_keyword = "time spent working"
    index = -1
    median = False
    ylabel = "Percentage Spent Working"
    fig = "perc"
    
    get_values(wanted_keyword, index, median, ylabel, fig)


# Run all
#time()
expl()
#perc()