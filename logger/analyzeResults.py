"""Process experiment logs into comparison figures and a LaTeX summary table.

Log layout expected (relative to repository root)::

    logger/logs/<SCENARIO>/[<LOAD>/]ALPHA-<n>/data_*_BETA-<b>_ALPHA-<n>_EB-<bool>.csv

For every ``ALPHA-<n>`` folder a single figure is produced. The figure holds one
subplot per configuration found in the folder (``EB-False`` and the
event-based runs ``BETA=0.005 / 0.01 / 0.015``). Two figures are generated per
folder: one for the controlled outputs (Y1, Y2 with their setpoints) and one for
the control signals (U1, U2).

Quality indicators are computed per channel for every CSV file:

* IAE  - integral of the absolute control error,            for Y1 and Y2
* IAU  - integral of the absolute value of the control,     for U1 and U2
* Tr   - settling time (5% band around the setpoint),       for step 1 and 2
* OS   - overshoot,                                          for step 1 and 2

The results are written to ``logger/analysis`` as PNG figures and a LaTeX table.
"""

import os
import re
import glob

import numpy as np
import pandas as pd
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

# numpy 2.x renamed ``trapz`` to ``trapezoid``; keep both working.
trapezoid = getattr(np, "trapezoid", getattr(np, "trapz", None))


# --- experiment constants -------------------------------------------------

# Nominal instant of the second setpoint step (first step happens at t = 0 s).
STEP1_TIME = 0.0
STEP2_TIME = 120.0

# Settling band: 5% of the (absolute) setpoint value.
SETTLING_FRACTION = 0.05

# Treat a setpoint change smaller than this as "no step" for that channel.
MIN_STEP_AMPLITUDE = 1e-6

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGS_DIR = os.path.join(REPO_ROOT, "logger", "logs")
OUTPUT_DIR = os.path.join(REPO_ROOT, "logger", "analysis")


# --- file discovery -------------------------------------------------------

def parseConfigFromName(filename):
    """Extract the BETA, ALPHA and EB values encoded in a log file name."""
    base = os.path.basename(filename)
    betaMatch = re.search(r"BETA-([0-9.]+)", base)
    alphaMatch = re.search(r"ALPHA-([0-9]+)", base)
    ebMatch = re.search(r"EB-(True|False)", base)
    beta = float(betaMatch.group(1)) if betaMatch else None
    alpha = int(alphaMatch.group(1)) if alphaMatch else None
    eventBased = (ebMatch.group(1) == "True") if ebMatch else False
    return {"beta": beta, "alpha": alpha, "eventBased": eventBased}


def configLabel(config):
    """Human readable label used for subplot titles and table rows."""
    if not config["eventBased"]:
        return "Time-triggered (EB-False)"
    return f"Event-based ($\\beta$={config['beta']})"


def configSortKey(config):
    """Order configurations: time-triggered first, then beta ascending."""
    return (1 if config["eventBased"] else 0, config["beta"] or 0.0)


def scenarioFromPath(filePath):
    """Return (scenario, load, alpha) derived from the file location."""
    relative = os.path.relpath(os.path.dirname(filePath), LOGS_DIR)
    parts = relative.split(os.sep)
    scenario = parts[0] if parts else ""
    alpha = parts[-1].replace("ALPHA-", "") if parts else ""
    load = "/".join(parts[1:-1]) if len(parts) > 2 else ""
    return scenario, load, alpha


def discoverGroups():
    """Group every CSV by its containing ``ALPHA-*`` folder."""
    pattern = os.path.join(LOGS_DIR, "**", "data_*.csv")
    files = sorted(glob.glob(pattern, recursive=True))
    groups = {}
    for filePath in files:
        folder = os.path.dirname(filePath)
        groups.setdefault(folder, []).append(filePath)
    return groups


# --- metric computation ---------------------------------------------------

def integralAbs(time, signal):
    """Trapezoidal integral of |signal| over the given (seconds) time axis."""
    return float(trapezoid(np.abs(signal), time))


def stepWindow(time, startTime, endTime=None):
    """Boolean mask selecting samples inside [startTime, endTime)."""
    if endTime is None:
        return time >= startTime
    return (time >= startTime) & (time < endTime)


def settlingTime(time, output, setpoint, startTime, endTime, setpointValue):
    """Time from the step instant until the output stays inside the 5% band.

    Returns ``None`` when the band is never reached or when the channel does not
    take a step in this window (handled by the caller).
    """
    mask = stepWindow(time, startTime, endTime)
    if not np.any(mask):
        return None

    localTime = time[mask]
    localOutput = output[mask]
    band = SETTLING_FRACTION * abs(setpointValue)
    if band == 0.0:
        return None

    inside = np.abs(localOutput - setpointValue) <= band
    # Find the last sample that is OUTSIDE the band; settling happens right after.
    outsideIdx = np.where(~inside)[0]
    if outsideIdx.size == 0:
        # Already inside the band for the whole window.
        return 0.0
    lastOutside = outsideIdx[-1]
    if lastOutside + 1 >= localTime.size:
        # Never settles within the window.
        return None
    return float(localTime[lastOutside + 1] - startTime)


def overshoot(time, output, startTime, endTime, setpointValue, previousSetpoint):
    """Overshoot for a step, expressed as a percentage of the step amplitude.

    Direction aware: a rising step measures the peak above the target, a falling
    step measures the dip below it. Returns ``None`` when there is no real step.
    """
    amplitude = setpointValue - previousSetpoint
    if abs(amplitude) < MIN_STEP_AMPLITUDE:
        return None

    mask = stepWindow(time, startTime, endTime)
    if not np.any(mask):
        return None
    localOutput = output[mask]

    if amplitude > 0:
        peak = np.max(localOutput)
        value = (peak - setpointValue) / amplitude * 100.0
    else:
        trough = np.min(localOutput)
        value = (setpointValue - trough) / abs(amplitude) * 100.0
    return float(max(value, 0.0))


def computeMetrics(df):
    """Compute every quality indicator for a single run."""
    time = df["Timestamp"].to_numpy(dtype=float)
    y1 = df["Y1"].to_numpy(dtype=float)
    y2 = df["Y2"].to_numpy(dtype=float)
    u1 = df["U1"].to_numpy(dtype=float)
    u2 = df["U2"].to_numpy(dtype=float)
    spY1 = df["SP_Y1"].to_numpy(dtype=float)
    spY2 = df["SP_Y2"].to_numpy(dtype=float)

    # Setpoint values for each step (taken from the data itself).
    step1Mask = stepWindow(time, STEP1_TIME, STEP2_TIME)
    step2Mask = stepWindow(time, STEP2_TIME)
    spY1Step1 = float(spY1[step1Mask][-1]) if np.any(step1Mask) else float(spY1[0])
    spY2Step1 = float(spY2[step1Mask][-1]) if np.any(step1Mask) else float(spY2[0])
    spY1Step2 = float(spY1[step2Mask][-1]) if np.any(step2Mask) else spY1Step1
    spY2Step2 = float(spY2[step2Mask][-1]) if np.any(step2Mask) else spY2Step1

    # Number of event invocations (triggered control updates) per channel.
    eventsY1 = int(df["Is_Event_Y1"].to_numpy(dtype=float).sum())
    eventsY2 = int(df["Is_Event_Y2"].to_numpy(dtype=float).sum())

    metrics = {
        "eventsY1": eventsY1,
        "eventsY2": eventsY2,
        "iaeY1": integralAbs(time, spY1 - y1),
        "iaeY2": integralAbs(time, spY2 - y2),
        "iauU1": integralAbs(time, u1),
        "iauU2": integralAbs(time, u2),
        "trStep1Y1": settlingTime(time, y1, spY1, STEP1_TIME, STEP2_TIME, spY1Step1),
        "trStep1Y2": settlingTime(time, y2, spY2, STEP1_TIME, STEP2_TIME, spY2Step1),
        "trStep2Y1": settlingTime(time, y1, spY1, STEP2_TIME, None, spY1Step2),
        "trStep2Y2": settlingTime(time, y2, spY2, STEP2_TIME, None, spY2Step2),
        "osStep1Y1": overshoot(time, y1, STEP1_TIME, STEP2_TIME, spY1Step1, 0.0),
        "osStep1Y2": overshoot(time, y2, STEP1_TIME, STEP2_TIME, spY2Step1, 0.0),
        "osStep2Y1": overshoot(time, y1, STEP2_TIME, None, spY1Step2, spY1Step1),
        "osStep2Y2": overshoot(time, y2, STEP2_TIME, None, spY2Step2, spY2Step1),
    }
    return metrics


# --- plotting -------------------------------------------------------------

def loadRun(filePath):
    return pd.read_csv(filePath, sep=";")


def plotGroup(folder, files, scenario, load, alpha):
    """Create the outputs & events figure for one ALPHA folder."""
    runs = []
    for filePath in files:
        config = parseConfigFromName(filePath)
        runs.append((config, filePath, loadRun(filePath)))
    runs.sort(key=lambda item: configSortKey(item[0]))

    loadSuffix = f" / {load}" if load else ""
    title = f"{scenario}{loadSuffix} / ALPHA-{alpha}"

    nameBase = f"{scenario}{('_' + load.replace('/', '_')) if load else ''}_ALPHA-{alpha}"

    _plotOutputs(runs, title, nameBase)


def _savefig(fig, nameBase, suffix):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    outPath = os.path.join(OUTPUT_DIR, f"{nameBase}_{suffix}.png")
    fig.savefig(outPath, dpi=150)
    plt.close(fig)
    print(f"[PLOT] {outPath}")


def _plotOutputs(runs, title, nameBase):
    """One figure with four subplots: Y1+SP, Y2+SP, events Y1, events Y2.

    Every configuration found in the folder is overlaid as a coloured curve so
    the runs can be compared directly. Setpoints are identical across configs
    and are drawn once as a dashed black line.
    """
    fig, axes = plt.subplots(4, 1, figsize=(11, 16), squeeze=False)
    axY1, axY2, axEv1, axEv2 = axes[0, 0], axes[1, 0], axes[2, 0], axes[3, 0]
    colors = plt.get_cmap("tab10").colors

    # Setpoint reference (same for every config) as a bold black line on top.
    _, _, refDf = runs[0]
    refTime = refDf["Timestamp"].to_numpy(dtype=float)
    axY1.plot(refTime, refDf["SP_Y1"], color="black", linewidth=2.5, zorder=10, label="SP Y1")
    axY2.plot(refTime, refDf["SP_Y2"], color="black", linewidth=2.5, zorder=10, label="SP Y2")

    for idx, (config, _, df) in enumerate(runs):
        color = colors[idx % len(colors)]
        time = df["Timestamp"].to_numpy(dtype=float)
        label = configLabel(config)
        axY1.plot(time, df["Y1"], color=color, linewidth=1, label=label)
        axY2.plot(time, df["Y2"], color=color, linewidth=1, label=label)
        axEv1.plot(time, np.cumsum(df["Is_Event_Y1"].to_numpy(dtype=float)),
                   color=color, linewidth=1.2, label=label)
        axEv2.plot(time, np.cumsum(df["Is_Event_Y2"].to_numpy(dtype=float)),
                   color=color, linewidth=1.2, label=label)

    axY1.set_title("Y1 + SP_Y1")
    axY1.set_ylabel("Y1")
    axY2.set_title("Y2 + SP_Y2")
    axY2.set_ylabel("Y2")
    axEv1.set_title("Zdarzenia Y1")
    axEv1.set_ylabel("Całkowita liczba zdarzeń Y1")
    axEv2.set_title("Zdfarzenia Y2")
    axEv2.set_ylabel("Całkowita liczba zdarzeń Y2")
    for axis in (axY1, axY2, axEv1, axEv2):
        axis.set_xlabel("Time [s]")
        axis.grid(True, alpha=0.3)
        axis.legend(loc="best", fontsize=8)

    fig.suptitle(f"{title} - sygnały i zdarzenia", fontsize=13)
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    _savefig(fig, nameBase, "outputs")


# --- LaTeX table ----------------------------------------------------------

def fmt(value, digits=4):
    if value is None or (isinstance(value, float) and np.isnan(value)):
        return "--"
    return f"{value:.{digits}f}"


def scenarioCaption(scenario, load):
    """Readable scenario name used in the table caption."""
    text = scenario.replace("_", "\\_")
    if load:
        text += " / " + load.replace("_", "\\_")
    return text


def buildLatexTable(scenario, load, records):
    """Return a LaTeX table for a single scenario (rows = ALPHA x config)."""
    caption = scenarioCaption(scenario, load)
    label = "tab:quality-" + re.sub(r"[^0-9A-Za-z]+", "-", f"{scenario}-{load}").strip("-").lower()

    header = (
        "% Auto-generated by logger/analyzeResults.py\n"
        "\\begin{table}[H]\n"
        "\\centering\n"
        "\\small\n"
        "\\setlength{\\tabcolsep}{4pt}\n"
        f"\\caption{{Wskaźniki jakości dla scenariusza {caption}.}}\n"
        f"\\label{{{label}}}\n"
        "\\begin{tabular}{l l "  # Alpha, Config
        "r r r r r r r r r r r r r r}\n"
        "\\toprule\n"
        "$\\alpha$ & Ustawienie & "
        "$N_{e,Y1}$ & $N_{e,Y2}$ & "
        "IAE$_{Y1}$ & IAE$_{Y2}$ & IAU$_{U1}$ & IAU$_{U2}$ & "
        "$T_{r1,Y1}$ & $T_{r1,Y2}$ & $T_{r2,Y1}$ & $T_{r2,Y2}$ & "
        "$M_{p1,Y1}$ & $M_{p1,Y2}$ & $M_{p2,Y1}$ & $M_{p2,Y2}$ \\\\\n"
        "\\midrule\n"
    )

    lines = []
    for rec in records:
        m = rec["metrics"]
        line = " & ".join([
            str(rec["alpha"]),
            configLabel(rec["config"]),
            str(m["eventsY1"]),
            str(m["eventsY2"]),
            fmt(m["iaeY1"]),
            fmt(m["iaeY2"]),
            fmt(m["iauU1"]),
            fmt(m["iauU2"]),
            fmt(m["trStep1Y1"], 2),
            fmt(m["trStep1Y2"], 2),
            fmt(m["trStep2Y1"], 2),
            fmt(m["trStep2Y2"], 2),
            fmt(m["osStep1Y1"], 2),
            fmt(m["osStep1Y2"], 2),
            fmt(m["osStep2Y1"], 2),
            fmt(m["osStep2Y2"], 2),
        ])
        lines.append(line + " \\\\")

    footer = (
        "\n\\bottomrule\n"
        "\\end{tabular}\n"
        "\\\\[2pt]\n"
        "\\footnotesize $N_e$ liczba wywołanych zdarzeń; IAE całka wartości bezwzględnej uchybu; IAU całka z wartości bezwzględnej sygnału sterującego; "
        "$T_r$ czas regulacji [s] (5\\% band); $M_p$ przeregulowanie [\\%]. "
        "\\end{table}\n"
    )
    return header + "\n".join(lines) + footer


def scenarioFileName(scenario, load):
    """Sanitised file name stem for a scenario's table."""
    stem = scenario if not load else f"{scenario}_{load.replace('/', '_')}"
    return "summary_" + re.sub(r"[^0-9A-Za-z_]+", "_", stem)


# --- entry point ----------------------------------------------------------

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    groups = discoverGroups()
    if not groups:
        print(f"[WARN] No log files found under {LOGS_DIR}")
        return

    records = []
    for folder, files in sorted(groups.items()):
        sampleScenario, load, alpha = scenarioFromPath(files[0])
        plotGroup(folder, files, sampleScenario, load, alpha)

        for filePath in sorted(files, key=lambda f: configSortKey(parseConfigFromName(f))):
            config = parseConfigFromName(filePath)
            df = loadRun(filePath)
            metrics = computeMetrics(df)
            records.append({
                "scenario": sampleScenario,
                "load": load,
                "alpha": alpha,
                "config": config,
                "metrics": metrics,
            })

    # One table per scenario (scenario + load combination).
    scenarioGroups = {}
    for rec in records:
        scenarioGroups.setdefault((rec["scenario"], rec["load"]), []).append(rec)

    for (scenario, load), groupRecords in sorted(scenarioGroups.items()):
        groupRecords.sort(key=lambda r: (int(r["alpha"]), configSortKey(r["config"])))
        latex = buildLatexTable(scenario, load, groupRecords)
        tablePath = os.path.join(OUTPUT_DIR, scenarioFileName(scenario, load) + ".tex")
        with open(tablePath, "w", encoding="utf-8") as handle:
            handle.write(latex)
        print(f"[TABLE] {tablePath}")

    print(f"[DONE] {len(records)} runs in {len(scenarioGroups)} scenario tables, output in {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
