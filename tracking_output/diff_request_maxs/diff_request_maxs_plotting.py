from matplotlib import pyplot as plt
import math

def get_values(textfile):
    tracking_lines = []
    tracking_values = []
    x_values = []
    y_values = []

    # get all tracking outputs
    with open(textfile) as file:
        for line in file:
            line.strip()
            if "[tracking]"  in line and not "queries" in line:
                tracking_lines.append(line)
    
    # delete line delimiter and [tracking] keyword
    for line in tracking_lines:
        line = line[:-1]
        line = line.split(" ")
        line = line[1:3] + line[4:]
        tracking_values.append(line)

    # probably later prettier but in basic the only necessary numbers are the timestamp line[0] and percentage line[-1]
    for line in tracking_values:
        x_values.append(float(line[0]))
        y_values.append(float(line[-1]))
        
    return [x_values, y_values]

def auslastung():
    textfiles = ["diff_request_maxs/output_n15_expl_r2.txt", "diff_request_maxs/output_n15_expl_r20.txt", "diff_request_maxs/output_n15_expl_r200.txt"]
    number = len(textfiles)
    all_values_temp1 = []
    all_values_temp2 = []
    all_values = []

    for i in range(0, number):
        values = get_values(textfiles[i])
        all_values_temp1.append(values[1])
    print(all_values_temp1)
    # delete percentage working
    for value_list in all_values_temp1:
        value_list = [value_list[0]] + [value_list [2]]
        all_values.append(value_list)
    print(all_values)

    max_value = math.ceil(max(max(sub_list) for sub_list in all_values))
    min_value = math.floor(min(min(sub_list) for sub_list in all_values))

    # sort correctly
    # for value_list in all_values_temp2:
    #     all_values[0].append(value_list[0])
    #     all_values[1].append(value_list[1])

    plt.boxplot(x=all_values, positions=[1,2,3], tick_labels=[2,20,200])
    print(max_value)
    print(min_value)
    plt.ylim([0.9, max_value])
    plt.xlim([0, 4])

    plt.title("Compare if changing the maximum \n number of jobs a worker can steal does anything")
    plt.ylabel("Aulastung")
    plt.xlabel("Maximum")

    plt.savefig("diff_request_maxs/plot_ausl.png")

def explored_nodes():
    textfiles = ["diff_request_maxs/output_n15_expl_r2.txt", "diff_request_maxs/output_n15_expl_r20.txt", "diff_request_maxs/output_n15_expl_r200.txt"]
    number = len(textfiles)
    all_values_temp1 = []
    all_values_temp2 = []
    all_values = []

    for i in range(0, number):
        values = get_values(textfiles[i])
        all_values_temp1.append(values[1])
    print(all_values_temp1)
    # delete percentage working
    for value_list in all_values_temp1:
        value_list = [value_list[1]] + [value_list [3]]
        all_values.append(value_list)
    print(all_values)

    max_value = math.ceil(max(max(sub_list) for sub_list in all_values))
    min_value = math.floor(min(min(sub_list) for sub_list in all_values))

    # sort correctly
    # for value_list in all_values_temp2:
    #     all_values[0].append(value_list[0])
    #     all_values[1].append(value_list[1])

    plt.boxplot(x=all_values, positions=[1,2,3], tick_labels=[2,20,200])
    print(max_value)
    print(min_value)
    plt.ylim([6000, 7000])
    plt.xlim([0, 4])

    plt.title("Compare if changing the maximum \n number of jobs a worker can steal does anything")
    plt.ylabel("Explored Nodes")
    plt.xlabel("Maximum")

    plt.savefig("diff_request_maxs/plot_nodes.png")

explored_nodes()