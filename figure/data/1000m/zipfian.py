import matplotlib.pyplot as plt
import numpy as np

# cache size is 10m
# threads are 16

zipfian_ratio = [0.8, 0.85, 0.9, 0.95, 0.99]

fifo = [655.1203, 581.2779, 470.7659, 380.1492, 288.6927]
lru = [785.8395, 733.7690, 584.4957, 491.5048, 402.5567]
frozenhot = [659.0179, 608.0936, 476.1560, 368.9393, 280.8990]
segment = [396.4501, 370.9183, 305.2824, 260.9618, 219.9786]

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
