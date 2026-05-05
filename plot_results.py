import os
import glob
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys

def get_latest_log_file():
    log_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logger', 'logs')
    log_files = glob.glob(os.path.join(log_dir, "**", 'data_*.csv'))
    if not log_files:
        return None
    latest_file = max(log_files, key=os.path.getctime)
    return latest_file

def plot_results(csv_file=None):
    if csv_file is None:
        csv_file = get_latest_log_file()
        if csv_file is None:
            print("No log files found in logger/logs directory.")
            return

    print(f"Plotting data from: {csv_file}")
    
    df = pd.read_csv(csv_file, delimiter=';')
    
    import json
    try:
        with open('config.json', 'r') as f:
            config = json.load(f)
            t_base_min = (config.get('T_BASE', 1800) / 1000.0) / 60.0
    except:
        t_base_min = 0.03
        
    time_axis = df.index * t_base_min
    
    y1 = df['Y1'].values
    y2 = df['Y2'].values
    sp_y1 = df['SP_Y1'].values
    sp_y2 = df['SP_Y2'].values
    u1 = df['U1'].values
    u2 = df['U2'].values
    is_event_y1 = df['Is_Event_Y1'].values
    is_event_y2 = df['Is_Event_Y2'].values
    
    # Extract event occurrence times
    event_indices_y1 = df.index[is_event_y1 == 1]
    event_times_y1 = time_axis[event_indices_y1]
    
    event_indices_y2 = df.index[is_event_y2 == 1]
    event_times_y2 = time_axis[event_indices_y2]
    
    fig, axs = plt.subplots(6, 1, figsize=(10, 16), sharex=True)
    
    # Plot y1
    axs[0].plot(time_axis, y1, 'b-', label='y1')
    axs[0].plot(time_axis, sp_y1, 'b--', label='Setpoint y1')
    axs[0].set_ylabel('y1')
    axs[0].legend(loc='upper right')
    axs[0].grid(True)

    # Plot y2
    axs[1].plot(time_axis, y2, 'r-', label='y2')
    axs[1].plot(time_axis, sp_y2, 'r--', label='Setpoint y2')
    axs[1].set_ylabel('y2')
    axs[1].legend(loc='upper right')
    axs[1].grid(True)
    
    # Plot u1
    axs[2].step(time_axis, u1, 'b-', where='post', label='u1')
    axs[2].set_ylabel('u1')
    axs[2].legend(loc='upper right')
    axs[2].grid(True)

    # Plot u2
    axs[3].step(time_axis, u2, 'r-', where='post', label='u2')
    axs[3].set_ylabel('u2')
    axs[3].legend(loc='upper right')
    axs[3].grid(True)
    
    # Plot Events y1
    axs[4].stem(event_times_y1, np.ones_like(event_times_y1), linefmt='b-', markerfmt='bo', basefmt='b-')
    axs[4].set_ylabel('Ey1')
    axs[4].set_yticks([0, 1])
    axs[4].set_yticklabels(['No', 'Yes'])
    axs[4].grid(True)

    # Plot Events y2
    axs[5].stem(event_times_y2, np.ones_like(event_times_y2), linefmt='r-', markerfmt='ro', basefmt='r-')
    axs[5].set_ylabel('Ey2')
    axs[5].set_xlabel('Time [min]')
    axs[5].set_yticks([0, 1])
    axs[5].set_yticklabels(['No', 'Yes'])
    axs[5].grid(True)
    
    plt.tight_layout()
    
    save_path = csv_file.replace('.csv', '.png')
    plt.savefig(save_path)
    print(f"Plot saved to: {save_path}")
    plt.show()

if __name__ == '__main__':
    if len(sys.argv) > 1:
        plot_results(sys.argv[1])
    else:
        plot_results()
