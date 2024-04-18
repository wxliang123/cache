import matplotlib.pyplot as plt
import numpy as np

# cache size is 10m
# threads are 32

read_ratio = [20, 50, 80, 100]

fifo = [290.2892, 300.7393, 299.7351, 301.4842]
lru = [522.3965, 593.8851, 651.8097, 677.6734]
frozenhot = [432.6407, 280.5520, 322.5336, 375.6852]
segment = [236.8762, 268.8086, 303.0859, 301.5046]

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
