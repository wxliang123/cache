import matplotlib.pyplot as plt
import numpy as np

# cache size is 10m
# threads are 16

zipfian_ratio = [0.8, 0.85, 0.9, 0.95, 0.99]

fifo = [430.3246, 367.8909, 306.0707, 228.5831, 184.8404]
lru = [660.7433, 586.6747, 516.9836, 415.1576, 345.0414]
frozenhot = [468.8553, 423.0961, 314.8082, 268.9887, 185.6710]
segment = [348.4486, 314.3443, 269.8895, 225.2598, 190.1708]

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

plt.ylabel('Running Time (s)')

plt.xticks(center, zipfian_ratio)
plt.xlabel('Zipfian Ratio')

plt.legend(loc='upper center', ncol=2)
plt.show()
