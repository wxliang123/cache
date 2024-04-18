import matplotlib.pyplot as plt
import numpy as np

# cache size is 10m
# data
zipfian_ratio = [0.8, 0.85, 0.9, 0.95, 0.99]

fifo = [24.26, 33.56, 44.87, 57.41, 67.38]
lru = [24.94, 34.30, 45.62, 58.09, 67.96]
frozenhot = [31.79, 41.33, 52.18, 64.28, 72.99]
segment = [26.00, 35.62, 47.15, 59.66, 69.43]

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
