from matplotlib import pyplot as plt
import math

from tracker_plotting import get_values

textfiles_two = ["diff_thr_nr/output_n15_expl_query_r200_thr2_1.txt", "diff_thr_nr/output_n15_expl_query_r200_thr2_2.txt",
                 "diff_thr_nr/output_n15_expl_query_r200_thr2_3.txt"]     
textfiles_four = ["diff_thr_nr/output_n15_expl_query_r200_thr4_1.txt", "diff_thr_nr/output_n15_expl_query_r200_thr4_2.txt",
                  "diff_thr_nr/output_n15_expl_query_r200_thr4_oldtracker_1.txt", "diff_thr_nr/output_n15_expl_query_r200_thr4_oldtracker_2.txt",
                  "diff_thr_nr/output_n15_expl_query_r200_thr4_oldtracker_3.txt"]

number_two = len(textfiles_two)
number_four = len(textfiles_four)

all_values_temp1_two = []
all_values_temp1_four = []
all_values_temp2_two = []
all_values_temp2_four = []
all_values = [[], []]

for i in range(0, number_two):
    values = get_values(textfiles_two[i])
    all_values_temp1_two.append(values[0])

for i in range(0, number_four):
    values = get_values(textfiles_four[i])
    all_values_temp1_four.append(values[0])

# delete percentage working
for value_list in all_values_temp1_two:
    all_values_temp2_two.append(value_list[0])
    all_values_temp2_two.append(value_list[1])
    all_values_temp2_two.append(value_list[2])
    all_values_temp2_two.append(value_list[3])

perc = []
expl = []
for i  in range(0, len(all_values_temp1_four)):
    for j in range(0,int(len(all_values_temp1_four[i]) / 2)):
        perc.append(all_values_temp1_four[i][2*j])
        expl.append(all_values_temp1_four[i][(2*j)+1])

#max_value = math.ceil(max(max(sub_list) for sub_list in all_values))
#min_value = math.floor(min(min(sub_list) for sub_list in all_values))

# sort correctly
#for value_list in all_values_temp2:
#    all_values[0].append(value_list[0])
#    all_values[1].append(value_list[1])
#print(all_values)

all_values = [all_values_temp2_two, expl+perc]
print(all_values)

#middle_two = sum(all_values[0]) / (number_two*4)
#middle_four = sum(all_values[1]) / (number_four*4)
#print(middle_two)
#print(middle_four)
#all_values=[[middle_two], [middle_four]]

plt.boxplot(x=all_values, tick_labels=[2,4])
plt.ylim([25, 75])
plt.xlim([0, 3])

plt.title("Compare if changing the number of threads \n does anything on a small example")
plt.ylabel("Time")
plt.xlabel("Number Of Threads")

plt.savefig("tracking_output/diff_thr_nr/diff_thr_nr_xx.png")

