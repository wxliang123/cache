# -*- coding: utf-8 -*-
# Drawing mutli sub bar figures
# CopyRight(c) Yongping Luo, All rights reserved!

import matplotlib as mpl
import matplotlib.pylab as plt
import matplotlib.ticker as mtick
import numpy as np
import os
import datetime
from matplotlib.transforms import Bbox

mpl.rcParams['font.family'] = 'Times New Roman'
mpl.rcParams['font.size'] = 12

letterfont = {
    'family': 'Times New Roman',
    'style': 'normal',
    'weight': 'normal',
    'color': 'black',
    'size': 10}

# change this to get different bar hatch
colorlist = ['white', 'lightgrey', 'lightblue', 'darkblue']

# control the distance between two groups, value in (0, 0.4)
group_distance = 0.2

# control the distance between two bars,  value in (0, 0.2)
bar_distance = 0.10


def plotax(ax, filepath, xlabel, ylabel, xlist, title, scaling):
    datalist = np.loadtxt(filepath)

    # the shape of the data is (n, 1), after a transpose, it turn to one dimension, which is not expected
    if len(datalist.shape) == 1:
        datalist = np.array([datalist])

    xcount = len(xlist)
    bar_count = len(datalist)
    x = np.array([i for i in range(xcount)], dtype=np.float32)

    bar_width = (1 - group_distance) / bar_count
    # if bar_count == 1:
    #     bar_width = (1 - group_distance) / bar_count
    # else :
    #     bar_width = (1 - group_distance) / bar_count - bar_distance / (bar_count - 1)

    # plot the bar first
    for i in range(bar_count):
        # plot bars
        ax.bar(x, datalist[i] / scaling, width=bar_width,
               edgecolor="black", color=colorlist[i])
        # update position to plot bar
        x += (1 - group_distance) / bar_count

    # plot xticks at the middle of bars, Do not modify here
    x = np.array([i for i in range(xcount)], dtype=np.float32)
    x = x + (1 - group_distance) / 2 - (1 - group_distance) / bar_count / 2
    ax.set_xticks(x)
    ax.set_xticklabels(xlist)
    ax.set_ylim(top=800)

    # plot grid
    ax.grid(which="major", axis="y", color='grey',
            linestyle='dotted', linewidth=0.5)

    # set title
    ax.set_title(title, fontdict=letterfont)
    # set label
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)


def plotbars(filepaths, xlabel, ylabel, xlist, legendlist, titles, scaling=1):
    width = 3 * len(filepaths)
    height = 3.5
    fig, axes = plt.subplots(1, len(filepaths), figsize=(width, height))

    # plot multi sub-figures
    for i in range(len(filepaths)):
        share_ylabel = ylabel if i == 0 else ''
        plotax(axes[i], filepaths[i], xlabel,
               share_ylabel, xlist, titles[i], scaling)

    # set glabol legend
    fig.legend(legendlist, loc='upper center',
               bbox_to_anchor=(0.5, 1), ncol=4, frameon=True)

    plt.subplots_adjust(top=0.8, bottom=0.15)
    plt.show()
    filepath_noext = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


if __name__ == "__main__":
    plotbars(
        ['100m.txt', '1000m.txt'],
        'Read Ratio (%)',
        'Running Time (s)',
        ['20', '50', '80', '100'],
        ["FIFO", "LRU", "ForzenHot", "SEMU"],
        ["(a).Normal scale", "(b).Large Scale"],
        1
    )
