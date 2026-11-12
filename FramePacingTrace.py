"""Read-only native update pacing capture; does not measure GPU/movie presents."""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import struct
import time

from SWTFU60 import GameMemory, find_game

ROOT = Path(__file__).resolve().parent
FIELDS = ('seconds', 'frame', 'game_seconds', 'real_seconds', 'timing_mode', 'cinematic_active',
          'game_dt', 'real_dt', 'cap', 'loading_job_pending', 'loading_job_joining',
          'loading_job_handle', 'read_seconds')


def summarize(rows):
    intervals, missed, resets = [], 0, 0
    previous = None
    changes = frames = 0
    for row in rows:
        if previous is None:
            previous = row
            continue
        delta = (row['frame']-previous['frame']) & 0xFFFFFFFF
        if not delta:
            continue
        if delta >= 0x80000000:
            resets += 1
            previous = row
            continue
        seconds = row['seconds']-previous['seconds']
        changes += 1
        frames += delta
        if delta == 1:
            intervals.append({'start': previous['seconds'], 'end': row['seconds'],
                              'milliseconds': seconds*1000,
                              'loading_job_pending': bool(previous['loading_job_pending'] or row['loading_job_pending']),
                              'cinematic_active': bool(previous.get('cinematic_active', 0) or row.get('cinematic_active', 0)),
                              'timing_mode': row['timing_mode']})
        else:
            missed += delta-1
        previous = row
    values = sorted(x['milliseconds'] for x in intervals)

    def percentile(fraction):
        return values[min(len(values)-1, math.ceil(fraction*len(values))-1)] if values else None

    return {'samples': len(rows), 'observed_counter_changes': changes,
            'advanced_frames': frames, 'missed_counter_transitions': missed,
            'counter_resets': resets, 'single_frame_intervals': len(intervals),
            'median_observed_ms': percentile(.5), 'p95_observed_ms': percentile(.95),
            'maximum_observed_ms': max(values) if values else None,
            'longest_observed_intervals': sorted(intervals, key=lambda x: x['milliseconds'], reverse=True)[:20],
            'scope': 'Sampled accepted-update counter only; not GPU/movie frame presentation. '
                     'Intervals have polling uncertainty. Skipped counter values are excluded from interval percentiles. '
                     'Loading/movie waits can legitimately stop this counter. Clock fields are unfenced observations.'}


def capture(game, seconds, poll_seconds):
    folder = ROOT/'analysis'/'pacing'/(datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')+f'-{game.pid}')
    folder.mkdir(parents=True)
    metadata = {'pid': game.pid, 'creation_time': game.creation_time, 'read_only': True,
                'requested_seconds': seconds, 'poll_seconds': poll_seconds,
                'utc': datetime.now(timezone.utc).isoformat(), 'before': game.inspect()}
    rows = []
    started = time.perf_counter()
    status = 'complete'
    try:
        while time.perf_counter()-started < seconds:
            tick = time.perf_counter()
            real, simulation, frame, _, mode = struct.unpack('<ffIII', game.read(0x124045C, 20))
            dt, real_dt = struct.unpack('<ff', game.read(0x10D4008, 8))
            flags = game.read(game.context+0xB2, 2)
            cinematic = game.read(0x12A1EF8, 1)[0]
            row = dict(zip(FIELDS, (tick-started, frame, simulation, real, mode, cinematic, dt, real_dt,
                                   game.f32(game.context+0x150), flags[0], flags[1],
                                   game.u32(0x1240478), time.perf_counter()-tick)))
            if not all(math.isfinite(row[key]) for key in ('game_seconds', 'real_seconds', 'game_dt', 'real_dt', 'cap')):
                raise RuntimeError('Non-finite clock observation; capture stopped')
            rows.append(row)
            if game.u32(0x1240440) != game.context:
                raise RuntimeError('Game context changed; capture stopped')
            time.sleep(poll_seconds)
    except KeyboardInterrupt:
        status = 'stopped by user'
    except (OSError, RuntimeError) as error:
        status = str(error)
    metadata.update({'status': status, 'elapsed_seconds': time.perf_counter()-started,
                     'summary': summarize(rows)})
    with (folder/'samples.csv').open('w', newline='', encoding='utf-8') as output:
        writer = csv.DictWriter(output, FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    (folder/'summary.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    return folder, metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int)
    parser.add_argument('--seconds', type=float, default=45.)
    parser.add_argument('--poll-ms', type=float, default=2.)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 300 or not 1 <= args.poll_ms <= 20:
        parser.error('Use 1..300 seconds and 1..20 milliseconds per poll')
    game = None
    try:
        game = GameMemory(args.pid or find_game())  # default read-only rights
        print(f'Recording native update timing for {args.seconds:g} seconds; switch to the game and reproduce the judder.', flush=True)
        folder, metadata = capture(game, args.seconds, args.poll_ms/1000)
        print(json.dumps({'output': str(folder), **metadata['summary'], 'status': metadata['status']}, indent=2))
    except (OSError, RuntimeError) as error:
        parser.exit(1, f'{error}\n')
    finally:
        if game:
            game.close()


if __name__ == '__main__':
    main()
