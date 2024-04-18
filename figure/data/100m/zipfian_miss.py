import matplotlib.pyplot as plt
import numpy as np

# cache size is 10m
# threads are 16

zipfian_ratio = [0.8, 0.85, 0.9, 0.95, 0.99]

fifo = [50, 58, 66, 74, 81]
lru = [51.03, 58.72, 66.93, 75.07, 81.08]
frozenhot = [56.48, 60.30, 71.19, 76.98, 83.59]
segment = [52.92, 60.74, 68.92, 76.86, 82.61]

width = 0.4

center = np.arange(len(zipfian_ratio))
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

plt.ylabel('Hit Ratio (%)')

plt.xticks(center, zipfian_ratio)
plt.xlabel('Zipfian Ratio')

plt.legend(loc='upper center', ncol=2)
plt.show()
