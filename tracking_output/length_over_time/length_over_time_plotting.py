from matplotlib import pyplot as plt
import math

from tracker_plotting import get_values

textfiles = [["length_over_time/output_n10_1.txt", "length_over_time/output_n10_expl.txt", "length_over_time/output_n10.txt"],
    ["length_over_time/output_n15_expl_query_r10_thr4_1.txt", "length_over_time/output_n15_expl_query_r10_thr4_2.txt",
     "length_over_time/output_n15_expl_query_r10_thr4_3.txt", "length_over_time/output_n15_expl_query_r100_thr4_1.txt",
     "length_over_time/output_n15_expl_query_r100_thr4_2.txt", "length_over_time/output_n15_expl_query_r100_thr4_3.txt",
     "length_over_time/output_n15_expl_query_r200_thr2_1.txt", "length_over_time/output_n15_expl_query_r200_thr2_2.txt",
     "length_over_time/output_n15_expl_query_r200_thr2_3.txt", "length_over_time/output_n15_expl_query_r200_thr4_1.txt",
     "length_over_time/output_n15_expl_query_r200_thr4_2.txt", "length_over_time/output_n15_expl_query_r200_thr4_oldtracker_1.txt",
     "length_over_time/output_n15_expl_query_r200_thr4_oldtracker_2.txt", "length_over_time/output_n15_expl_query_r200_thr4_oldtracker_3.txt",
     "length_over_time/output_n15_expl_query_r1000_thr4_1.txt", "length_over_time/output_n15_expl_query_r1000_thr4_2.txt",
     "length_over_time/output_n15_expl_query_r1000_thr4_3.txt", "length_over_time/output_n15_expl_r2.txt", "length_over_time/output_n15_expl_r20.txt",
     "length_over_time/output_n15_expl_r200.txt", "length_over_time/output_n15.txt"],
    ["length_over_time/output_n20_1.txt"],
    ["length_over_time/output_n25_1.txt", "length_over_time/output_n25_2.txt", "length_over_time/output_n25_3.txt",
     "length_over_time/output_n25_4.txt", "length_over_time/output_n25_5.txt"]]

values = [[], [], [], []]
for i in range(0, len(textfiles)):
    for j in range(0, len(textfiles[i])):
        temp_values = get_values(textfiles[i][j])
        values[i].append(temp_values[0])
#print(values)

sorted_values = [[], [], [], []]
for i in range(0, len(values)):
    for j in range(0, len(values[i])):
        for k in range(0, len(values[i][j])):
            sorted_values[i].append(values[i][j][k])
#print(sorted_values)

max_value = math.ceil(max(max(sub_list) for sub_list in sorted_values))
min_value = math.floor(min(min(sub_list) for sub_list in sorted_values))

plt.boxplot(x=sorted_values, tick_labels=[10,15,20,25])
plt.ylim([0, 100])
plt.xlim([0, 5])

plt.title("Compare computing time for length of input")
plt.ylabel("Time")
plt.xlabel("Input Length n")

plt.savefig("length_over_time/length_over_time_plot.png")