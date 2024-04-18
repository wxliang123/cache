import matplotlib.pyplot as plt
import numpy as np

# data
read_ratio = [20, 50, 80, 100]

fifo = [470.6489, 474.9468, 471.2591, 481.8506]
lru = [532.2292, 559.5595, 608.9634, 639.1471]
frozenhot = [359.6686, 481.0525, 451.8347, 465.3648]
segment = [229.6715, 254.6160, 279.8289, 260.2760]

width = 0.4

center = np.arange(len(read_ratio))
fifo_pos = center - 3*width/4
lru_pos = center - width/4
frozenhot_pos = center + width/4
segment_pos = center + 3*width/4

plt.bar(fifo_pos, fifo, width/2, label='fifo cache',
        color='white', edgecolor='black')
plt.bar(lru_pos, lru, width/2, label='lru cache',
        color='lightgrey', edgecolor='black')
plt.bar(frozenhot_pos, frozenhot, width/2, label='frozenhot cache',
        color='lightblue', edgecolor='black')
plt.bar(segment_pos, segment, width/2, label='segment cache',
        color='darkblue', edgecolor='black')

plt.ylabel('Running Time (s)')

plt.xticks(center, read_ratio)
plt.xlabel('Read Ratio (%)')

plt.legend(loc='upper center', ncol=2)
plt.show()
