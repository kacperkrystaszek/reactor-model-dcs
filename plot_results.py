import os
import glob
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys
import json

def calculate_metrics(y, sp, time_axis, beta):
    # IAE
    dt = np.diff(time_axis, prepend=time_axis[0])
    if len(dt) > 0:
        dt[0] = dt[1] if len(dt) > 1 else 0.0
    iae = np.sum(np.abs(y - sp) * dt)
    
    # Settling time
    in_band = np.abs(y - sp) <= beta
    out_of_band_indices = np.where(~in_band)[0]
    if len(out_of_band_indices) == 0:
        settling_time = 0.0
    else:
        last_out_idx = out_of_band_indices[-1]
        if last_out_idx < len(time_axis) - 1:
            settling_time = time_axis[last_out_idx + 1]
        else:
            settling_time = float('inf') # Never settled
            
    # Overshoot based on BETA limit
    max_err = np.max(np.abs(y - sp))
    overshoot_beyond_beta = max(0.0, max_err - beta)
        
    return iae, settling_time, max_err, overshoot_beyond_beta

def process_csv(csv_file):
    print(f"Processing: {csv_file}")
    df = pd.read_csv(csv_file, delimiter=';')
    
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
    
    beta_y1 = df['BETA_Y1'].values[0] if 'BETA_Y1' in df.columns else 0.05
    beta_y2 = df['BETA_Y2'].values[0] if 'BETA_Y2' in df.columns else 0.05
    hmax_y1 = df['HMAX_Y1'].values[0] if 'HMAX_Y1' in df.columns else 0
    hmax_y2 = df['HMAX_Y2'].values[0] if 'HMAX_Y2' in df.columns else 0
    
    # Calculate metrics
    iae_y1, st_y1, max_err_y1, over_y1 = calculate_metrics(y1, sp_y1, time_axis, beta_y1)
    iae_y2, st_y2, max_err_y2, over_y2 = calculate_metrics(y2, sp_y2, time_axis, beta_y2)
    
    # Extract event occurrence times
    event_indices_y1 = df.index[is_event_y1 == 1]
    event_times_y1 = time_axis[event_indices_y1]
    
    event_indices_y2 = df.index[is_event_y2 == 1]
    event_times_y2 = time_axis[event_indices_y2]
    
    fig, axs = plt.subplots(6, 1, figsize=(10, 16), sharex=True)
    
    # Plot y1
    axs[0].plot(time_axis, y1, 'b-', label='y1')
    axs[0].plot(time_axis, sp_y1, 'b--', label='Setpoint y1')
    axs[0].axhline(sp_y1[-1] + beta_y1, color='g', linestyle=':', alpha=0.5, label='BETA Band')
    axs[0].axhline(sp_y1[-1] - beta_y1, color='g', linestyle=':', alpha=0.5)
    axs[0].set_ylabel('y1')
    axs[0].legend(loc='upper right')
    axs[0].grid(True)

    # Plot y2
    axs[1].plot(time_axis, y2, 'r-', label='y2')
    axs[1].plot(time_axis, sp_y2, 'r--', label='Setpoint y2')
    axs[1].axhline(sp_y2[-1] + beta_y2, color='g', linestyle=':', alpha=0.5, label='BETA Band')
    axs[1].axhline(sp_y2[-1] - beta_y2, color='g', linestyle=':', alpha=0.5)
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
    if len(event_times_y1) > 0:
        axs[4].stem(event_times_y1, np.ones_like(event_times_y1), linefmt='b-', markerfmt='bo', basefmt='b-')
    axs[4].set_ylabel('Ey1')
    axs[4].set_yticks([0, 1])
    axs[4].set_yticklabels(['No', 'Yes'])
    axs[4].grid(True)

    # Plot Events y2
    if len(event_times_y2) > 0:
        axs[5].stem(event_times_y2, np.ones_like(event_times_y2), linefmt='r-', markerfmt='ro', basefmt='r-')
    axs[5].set_ylabel('Ey2')
    axs[5].set_xlabel('Time [min]')
    axs[5].set_yticks([0, 1])
    axs[5].set_yticklabels(['No', 'Yes'])
    axs[5].grid(True)
    
    plt.tight_layout()
    
    png_path = csv_file.replace('.csv', '.png')
    plt.savefig(png_path)
    plt.close(fig)
    print(f"Plot saved to: {png_path}")
    
    txt_path = csv_file.replace('.csv', '_metrics.txt')
    with open(txt_path, 'w', encoding='utf-8') as f:
        f.write("--- Wskazniki Jakosci ---\n\n")
        f.write("Parametry regulacji:\n")
        f.write(f"BETA_Y1: {beta_y1}\n")
        f.write(f"BETA_Y2: {beta_y2}\n")
        f.write(f"HMAX_Y1: {hmax_y1}\n")
        f.write(f"HMAX_Y2: {hmax_y2}\n\n")
        f.write("Wyjscie Y1:\n")
        f.write(f"- IAE: {iae_y1:.4f}\n")
        st_str = f"{st_y1:.2f} min" if st_y1 != float('inf') else "Nigdy"
        f.write(f"- Czas regulacji: {st_str}\n")
        f.write(f"- Maksymalny uchyb: {max_err_y1:.4f}\n")
        f.write(f"- Przeregulowanie ponad BETA: {over_y1:.4f}\n\n")
        
        f.write("Wyjscie Y2:\n")
        f.write(f"- IAE: {iae_y2:.4f}\n")
        st_str2 = f"{st_y2:.2f} min" if st_y2 != float('inf') else "Nigdy"
        f.write(f"- Czas regulacji: {st_str2}\n")
        f.write(f"- Maksymalny uchyb: {max_err_y2:.4f}\n")
        f.write(f"- Przeregulowanie ponad BETA: {over_y2:.4f}\n\n")
        
        f.write(f"Ilosc eventow Y1: {len(event_times_y1)}\n")
        f.write(f"Ilosc eventow Y2: {len(event_times_y2)}\n")
        
    print(f"Metrics saved to: {txt_path}\n")

def process_all_files():
    log_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logger', 'logs')
    csv_files = glob.glob(os.path.join(log_dir, "**", 'data_*.csv'), recursive=True)
    
    if not csv_files:
        print("No log files found in logger/logs directory.")
        return
        
    for csv_file in csv_files:
        png_file = csv_file.replace('.csv', '.png')
        txt_file = csv_file.replace('.csv', '_metrics.txt')
        
        if not os.path.exists(png_file) or not os.path.exists(txt_file):
            process_csv(csv_file)
        else:
            print(f"Skipping {csv_file} (Already processed)")

if __name__ == '__main__':
    if len(sys.argv) > 1:
        # Process a specific file
        process_csv(sys.argv[1])
    else:
        # Process all that are missing plots/metrics
        process_all_files()
