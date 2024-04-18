import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def get_data_from_csv(csvfile: str, Type: str, data_type: str):
    df = pd.read_csv(csvfile)
    df = df[df["type"] == Type]
    li = []
    for x in df[[data_type]].values.flatten():
        li.append(x)
    return np.array(li)


if __name__ == "__main__":
    mpl.rcParams["figure.figsize"] = (14, 8)
    mpl.rcParams["font.size"] = 24
    csvfile = "evaluation-update.csv"
    fig = plt.figure()
    step = 0.2

    ax1 = fig.add_subplot(2, 1, 1)
    # plt.subplot(1, 2, 2)

    baseline_step = get_data_from_csv(csvfile, "LRU", "step")
    # baseline_step = np.array([x for x in baseline_step if x < np.max(baseline_step) * 0.95])
    baseline_step = baseline_step / np.max(baseline_step)
    baseline_hit_ratio = get_data_from_csv(csvfile, "LRU", "hit ratio")
    # baseline_hit_ratio = baseline_hit_ratio[:len(baseline_step)]

    global_fh_step = get_data_from_csv(csvfile, "global LRU FH", "step")
    # global_fh_step = np.array([x for x in global_fh_step if x < np.max(global_fh_step) * 0.95])
    global_fh_step = global_fh_step / np.max(global_fh_step)
    global_fh_hit_ratio = get_data_from_csv(csvfile, "global LRU FH", "hit ratio")
    # global_fh_hit_ratio = global_fh_hit_ratio[:len(global_fh_step)]

    fh_step = get_data_from_csv(csvfile, "LRU FH", "step")
    # fh_step = np.array([x for x in fh_step if x < np.max(fh_step) * 0.95])
    fh_step = fh_step / np.max(fh_step)
    fh_hit_ratio = get_data_from_csv(csvfile, "LRU FH", "hit ratio")
    # fh_hit_ratio = fh_hit_ratio[:len(fh_step)]

    redis_step = get_data_from_csv(csvfile, "Redis LRU", "step")
    # redis_step = np.array([x for x in redis_step if x < np.max(redis_step) * 0.95])
    redis_step = redis_step / np.max(redis_step)
    redis_hit_ratio = get_data_from_csv(csvfile, "Redis LRU", "hit ratio")
    # redis_hit_ratio = redis_hit_ratio[:len(redis_step)]

    fig_d_y_baseline_max = np.max(baseline_hit_ratio)
    fig_d_y_global_max = np.max(global_fh_hit_ratio)
    fig_d_y_fh_max = np.max(fh_hit_ratio)
    fig_d_y_redis_max = np.max(redis_hit_ratio)
    
    fig_d_y_baseline_min = np.min(baseline_hit_ratio)
    fig_d_y_global_min = np.min(global_fh_hit_ratio)
    fig_d_y_fh_min = np.min(fh_hit_ratio)
    fig_d_y_redis_min = np.min(redis_hit_ratio)

    y_max = max(fig_d_y_fh_max, fig_d_y_baseline_max, fig_d_y_global_max, fig_d_y_redis_max) * 1.2
    y_min = min(fig_d_y_fh_min, fig_d_y_baseline_min, fig_d_y_global_min, fig_d_y_redis_min) * 0.8

    fh_status = get_data_from_csv(csvfile, "global LRU FH", "episode")
    baseline_color_bar = [1.2]
    frozen_color_bar = [0]
    learning_color_bar = [0]
    draw_bg_color_step = [fh_step[0] / 2]
    draw_width = [fh_step[0]]
    for i in range(len(fh_step) - 1):
        point_1 = fh_step[i]
        point_2 = fh_step[i + 1]
        status_1 = fh_status[i]
        status_2 = fh_status[i + 1]
        draw_bg_color_step.append((point_1 + point_2) / 2)
        draw_width.append(point_2 - point_1)
        baseline_color_bar.append(0 if status_2 != "baseline" else 1.2)
        frozen_color_bar.append(0 if status_2 != "frozen" else 1.2)
        learning_color_bar.append(0 if status_2 != "learning" else 1.2)

    bar1 = ax1.bar(
        draw_bg_color_step,
        np.array(baseline_color_bar) * 1000,
        np.array(draw_width) + 0.001,
        color="0.55",
    )
    bar3 = ax1.bar(
        draw_bg_color_step,
        np.array(frozen_color_bar) * 1000,
        np.array(draw_width) + 0.001,
        color="0.75",
    )
    bar2 = ax1.bar(
        draw_bg_color_step,
        np.array(learning_color_bar) * 1000,
        np.array(draw_width) + 0.001,
        color="0.90",
    )
    

    ax1.text(0.08, 1.1, "1.0")
    ax1.text(0.27, 1.1, "1.0")
    ax1.text(0.48, 1.1, "1.0")
    ax1.text(0.69, 1.1, "1.0")
    ax1.text(0.88, 1.1, "1.0")

    total_lru_hit_ratio = get_data_from_csv(csvfile, "total LRU", "hit ratio")[0]
    total_redis_hit_ratio = get_data_from_csv(csvfile, "total Redis LRU", "hit ratio")[0]
    total_fh_hit_ratio = get_data_from_csv(csvfile, "total LRU FH", "hit ratio")[0]
    fc_ratio = get_data_from_csv(csvfile, "total LRU FH", "episode")[0]

    line1, = ax1.plot(
        baseline_step,
        baseline_hit_ratio,
        # color="black",
        linestyle="solid"
    )
    line2, = ax1.plot(
        redis_step,
        redis_hit_ratio,
        # color="black",
        linestyle="dotted"
    )
    line3, = ax1.plot(
        global_fh_step,
        global_fh_hit_ratio,
        # color="black",
        linestyle='dashed'
    )
    
    ax1.set_ylabel("Hit ratio", labelpad=0.5)
    ax1.set_xlabel("Request progress")
    ax1.set_ylim(0.8, 1.2 if y_max > 0.9 else 1)
    
    ax1.set_xticks(np.array([0, 0.2, 0.4, 0.6, 0.8, 1.0]) / 1)
    ax1.set_xlim(draw_bg_color_step[0], 1.0)
    ax1.set_yticks([0.8, 1.0, 1.2])
    ax1.set_xticklabels(['0%', '20%', '40%', '60%', '80%', '100%'])
    ax1.set_yticklabels(["80%", "100%", ""])

    ax1.legend([line1, bar1, line2, bar2, line3, bar3], ['Relaxed-LRU', 'Learning', 'Sampling', 'Construction', 'LRU-FH', 'Frozen'], loc=(0, 1), frameon=False, ncol=3) # bbox_to_anchor=(-0.4, 1.2)
    plt.tight_layout()
    fig.subplots_adjust(right=0.90)

    lru_time = get_data_from_csv(csvfile, "LRU", "step")
    fh_time = get_data_from_csv(csvfile, "LRU FH", "step")
    redis_time = get_data_from_csv(csvfile, "Redis LRU", "step")

    lru_throughput_ = get_data_from_csv(csvfile, "LRU", "thput") / 1000000
    fh_throughput_ = get_data_from_csv(csvfile, "LRU FH", "thput") / 1000000
    redis_throughput_ = get_data_from_csv(csvfile, "Redis LRU", "thput") / 1000000

    y_max = max(
        np.max(lru_throughput_),
        np.max(fh_throughput_),
        np.max(redis_throughput_)
    ) * 1.2
    y_min = min(
        np.min(lru_throughput_),
        np.min(fh_throughput_),
        np.min(redis_throughput_)
    ) * 0.8

    lru_time = lru_time / np.max(lru_time)
    fh_time = fh_time / np.max(fh_time)
    redis_time = redis_time / np.max(redis_time)

    ax2 = fig.add_subplot(2, 1, 2)

    lru_throughput_ = get_data_from_csv(csvfile, "LRU", "thput") / 1000000
    lru_time = get_data_from_csv(csvfile, "LRU", "step")
    fh_throughput_ = get_data_from_csv(csvfile, "LRU FH", "thput") / 1000000
    fh_time = get_data_from_csv(csvfile, "LRU FH", "step")
    redis_throughput_ = get_data_from_csv(csvfile, "Redis LRU", "thput") / 1000000
    redis_time = get_data_from_csv(csvfile, "Redis LRU", "step")

    lru_time = lru_time / np.max(lru_time)
    fh_time = fh_time / np.max(fh_time)
    redis_time = redis_time / np.max(redis_time)

    total_lru_throughput = (get_data_from_csv(csvfile, "total LRU", "thput") / 1000000)[0]
    total_fh_throughput = (get_data_from_csv(csvfile, "total LRU FH", "thput") / 1000000)[0]
    total_redis_throughput = (get_data_from_csv(csvfile, "total Redis LRU", "thput") / 1000000)[0]

    ax2.plot(
        lru_time,
        lru_throughput_,
        # color="black",
        label="LRU (" + format(total_lru_throughput, '.2f') + ")",
        linestyle="solid"
    )
    ax2.plot(
        redis_time,
        redis_throughput_,
        # color="black",
        label="Redis LRU (" + format(total_redis_throughput, '.2f') + ")",
        linestyle="dotted"
    )
    ax2.plot(
        fh_time,
        fh_throughput_,
        # color="black",
        label="LRU FH (" + format(total_fh_throughput, '.2f') + ")",
        linestyle='dashed'
    )

    ax2.bar(
        draw_bg_color_step,
        np.array(baseline_color_bar) * 1000,
        np.array(draw_width) + 0.001,
        color="0.55",
    )
    ax2.bar(
        draw_bg_color_step,
        np.array(frozen_color_bar) * 1000,
        np.array(draw_width) + 0.001,
        color="0.75",
    )
    ax2.bar(
        draw_bg_color_step,
        np.array(learning_color_bar) * 1000,
        np.array(draw_width) + 0.001,
        color="0.90",
    )
    

    plt.xticks(np.array([0, 0.2, 0.4, 0.6, 0.8, 1.0,]) / 1, labels=['0%', '20%', '40%', '60%', '80%', '100%'])
    ax2.set_ylabel("Throughput (MQPS)", labelpad=26)
    ax2.set_ylim(0, y_max)
    plt.xlim(draw_bg_color_step[0], 1.0)
    plt.tight_layout()

    plt.subplots_adjust(hspace=-0.2)
    plt.tight_layout()

    plt.savefig("evaluation-all-update.pdf")
