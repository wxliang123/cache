import matplotlib.pyplot as plt
import numpy as np

# cache size is 10m
# threads are 16

read_ratio = [20, 50, 80, 100]

fifo = [292.0418, 298.0366, 306.0707, 292.6616]
lru = [400.4379, 454.8298, 516.9836, 541.6239]
frozenhot = [343.4873, 301.4523, 314.8082, 341.9723]
segment = [281.2466, 270.6867, 269.8895, 286.3232]

width = 0.4

center = np.arange(len(read_ratio))
fifo_pos = center - 3*width/4
lru_pos = center - width/4
frozenhot_pos = center + width/4
segment_pos = center + 3*width/4

plt.figure(figsize=(7, 6))

plt.bar(fifo_pos, fifo, width/2, label='FIFO',
        color='white', edgecolor='black')
plt.bar(lru_pos, lru, width/2, label='LRU',
        color='lightgrey', edgecolor='black')
plt.bar(frozenhot_pos, frozenhot, width/2, label='FrozenHot',
        color='lightblue', edgecolor='black')
plt.bar(segment_pos, segment, width/2, label='SEMU',
        color='darkblue', edgecolor='black')

plt.ylim(top=600)
plt.yticks(fontsize=16)
plt.ylabel('Running Time (s)', fontsize=16)

plt.xticks(center, read_ratio, fontsize=16)
plt.xlabel('Read Ratio (%)', fontsize=16)

plt.legend(loc='upper center', ncol=4, fontsize=12)
plt.show()
