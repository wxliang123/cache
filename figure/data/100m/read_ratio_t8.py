import matplotlib.pyplot as plt
import numpy as np

# cache size is 10m
# threads are 16

read_ratio = [20, 50, 80, 100]

fifo = [289.8830, 298.8104, 331.2344, 345.9495]
lru = [350.7009, 379.9515, 428.6127, 464.7320]
frozenhot = [296.7443, 297.9932, 314.0464, 354.4343]
segment = [203.2212, 260.7531, 310.1275, 345.2780]

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
