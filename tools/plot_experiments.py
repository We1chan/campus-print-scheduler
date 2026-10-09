#!/usr/bin/env python3
"""Render standalone figures from an audited experiment run (matplotlib required)."""
import argparse
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

POLICIES = ['fcfs', 'sjf', 'balanced']
LABELS = {'fcfs': 'FCFS', 'sjf': 'SJF', 'balanced': 'Balanced'}
COLORS = {'fcfs': '#4477AA', 'sjf': '#EE6677', 'balanced': '#228833'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    rows = json.loads((args.run / 'summary.json').read_text(encoding='utf-8'))
    args.output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10,
                         'axes.spines.top': False, 'axes.spines.right': False, 'svg.hashsalt': 'campus-print-A'})

    def save(fig, name):
        fig.savefig(args.output / f'{name}.png', dpi=160, bbox_inches='tight')
        fig.savefig(args.output / f'{name}.svg', bbox_inches='tight', metadata={'Date': None})
        plt.close(fig)

    baseline = [r for r in rows if r['family'] == 'windows' and r['priority_mode'] == 'job-id-modulo']
    offsets = sorted({r['offset'] for r in baseline})
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.5), constrained_layout=True)
    for ax, metric, title in zip(axes, ['average_first_wait_ms', 'max_first_wait_ms'], ['Mean waiting time', 'Maximum waiting time']):
        for i, policy in enumerate(POLICIES):
            values = [next(r[metric] / 1000 for r in baseline if r['offset'] == offset and r['policy'] == policy) for offset in offsets]
            ax.bar([x + (i - 1) * .24 for x in range(len(offsets))], values, width=.23, color=COLORS[policy], label=LABELS[policy])
        ax.set_xticks(range(len(offsets)), [f'Window {i+1}\noffset {o}' for i, o in enumerate(offsets)])
        ax.set_title(title); ax.set_ylabel('Simulated seconds'); ax.grid(axis='y', alpha=.2); ax.set_axisbelow(True)
    axes[0].legend(fontsize=9)
    fig.suptitle('SDSC-mapped workloads: 1,000 jobs/window, 2 printers, 150 ms/sheet\nLegacy priority metadata is ignored; balanced aging interval = 30 s')
    save(fig, 'window-comparison')

    manifest = json.loads((args.run / 'manifest.json').read_text(encoding='utf-8'))
    case = next(c for c in manifest['cases'] if c['name'] == 'sdsc-offset0-job-id-modulo')
    report = json.loads((args.run / case['report']).read_text(encoding='utf-8'))
    fig, ax = plt.subplots(figsize=(8, 4.5), constrained_layout=True)
    for result in report['results']:
        values = sorted(j['waiting_ms']/1000 for j in result['jobs'])
        ax.step(values, [(i+1)/len(values) for i in range(len(values))], where='post',
                color=COLORS[result['policy']], label=LABELS[result['policy']])
    ax.set(xlabel='Waiting time (simulated seconds)', ylabel='Fraction of completed tasks',
           title='Waiting-time distribution: first 1,000-job window', ylim=(0, 1.02))
    ax.grid(alpha=.2); ax.legend(); save(fig, 'waiting-distribution')

    density = [r for r in rows if r['family'] == 'density' or
               (r['family'] == 'windows' and r['offset'] == 0 and r['priority_mode'] == 'job-id-modulo')]
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.5), constrained_layout=True)
    for policy in POLICIES:
        group = sorted([r for r in density if r['policy'] == policy], key=lambda r: float(r['time_scale']))
        x = [float(r['time_scale']) for r in group]
        axes[0].plot(x, [r['average_first_wait_ms']/1000 for r in group], 'o-', color=COLORS[policy], label=LABELS[policy])
        axes[1].plot(x, [r['utilization']*100 for r in group], 'o-', color=COLORS[policy], label=LABELS[policy])
    for ax in axes:
        ax.set_xscale('log'); ax.set_xticks([.002,.01,.05], ['0.002','0.01','0.05']); ax.set_xlabel('Arrival-time scale (larger = lighter load)'); ax.grid(alpha=.2)
    axes[0].set_ylabel('Mean wait (simulated seconds)'); axes[1].set_ylabel('Printer-slot utilization (%)'); axes[0].legend(fontsize=9)
    fig.suptitle('Load sensitivity: same first 1,000 source jobs and sheet counts')
    save(fig, 'load-sensitivity')

    pressure = [r for r in rows if r['family'] == 'aging-pressure']
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.5), constrained_layout=True)
    for policy in POLICIES:
        group = sorted([r for r in pressure if r['policy'] == policy], key=lambda r: r['aging_ms'])
        x = [r['aging_ms']/1000 for r in group]
        axes[0].plot(x, [r['low_priority_wait_ms']/1000 for r in group], 'o-', label=LABELS[policy], color=COLORS[policy])
        axes[1].plot(x, [r['average_first_wait_ms']/1000 for r in group], 'o-', label=LABELS[policy], color=COLORS[policy])
    for ax in axes:
        ax.set_xscale('log'); ax.set_xticks([1,30,120], ['1','30','120']); ax.set_xlabel('Waiting-score reference interval (seconds)'); ax.grid(alpha=.2)
    axes[0].set_ylabel('Large job wait (seconds)'); axes[1].set_ylabel('All-task mean wait (seconds)'); axes[0].legend(fontsize=9)
    fig.suptitle('Finite pressure scenario: 1 large task + 40 small tasks\n2 printers; 20 arrival waves, 1.5 seconds apart')
    save(fig, 'aging-pressure')


if __name__ == '__main__':
    main()
