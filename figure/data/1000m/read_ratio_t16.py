import matplotlib.pyplot as plt
import numpy as np

# data
read_ratio = [20, 50, 80, 100]

fifo = [467.2519, 471.2056, 470.7659, 481.2997]
lru = [525.2922, 581.3752, 584.4957, 616.0394]
frozenhot = [470.0873, 495.2340, 476.1560, 479.2454]
segment = [281.2651, 264.7720, 305.2824, 336.4809]

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
