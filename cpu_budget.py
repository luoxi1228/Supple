#!/usr/bin/env python3
"""CPU budget shared by build configuration and experiment runners."""
import argparse
import os
from pathlib import Path
import xml.etree.ElementTree as ET


def quota_budget(text, period=None):
    try:
        values = text.split() if period is None else (text.strip(), period.strip())
        if len(values) != 2 or values[0] == "max":
            return None
        quota, interval = map(int, values)
        return (quota + interval - 1) // interval if quota > 0 and interval > 0 else None
    except (ValueError, ZeroDivisionError):
        return None


def available_cpus(affinity=None, online=None, read=None):
    read = read or (lambda path: Path(path).read_text())
    if affinity is None:
        try:
            affinity = len(os.sched_getaffinity(0))
        except (AttributeError, OSError):
            affinity = 0
    if not affinity:
        if online is None:
            try:
                online = os.sysconf('SC_NPROCESSORS_ONLN')
            except (ValueError, OSError, AttributeError):
                online = os.cpu_count()
        affinity = online
    budget = max(1, affinity or 1)
    groups = {}
    try:
        for line in read('/proc/self/cgroup').splitlines():
            _, controllers, path = line.split(':', 2)
            for controller in controllers.split(','):
                groups[controller] = path
    except (OSError, ValueError):
        pass
    locations = [(Path('/sys/fs/cgroup'), Path('/sys/fs/cgroup'), True)]
    try:
        for line in read('/proc/self/mountinfo').splitlines():
            before, after = line.split(' - ', 1)
            fields, mount = before.split(), after.split()
            unified = mount[0] == 'cgroup2'
            if not unified and not (mount[0] == 'cgroup' and 'cpu' in mount[2].split(',')):
                continue
            root = fields[3].replace('\\040', ' ')
            point = Path(fields[4].replace('\\040', ' '))
            group = groups.get('' if unified else 'cpu', '/')
            if root == '/':
                relative = group.lstrip('/')
            elif group == root or group.startswith(root + '/'):
                relative = group[len(root):].lstrip('/')
            else:
                relative = ''  # Only the mount root is visible in this namespace.
            locations.append((point / relative, point, unified))
    except (OSError, ValueError, IndexError):
        pass
    for location, root, unified in locations:
        while True:
            try:
                limit = (quota_budget(read(str(location / 'cpu.max'))) if unified else
                         quota_budget(read(str(location / 'cpu.cfs_quota_us')),
                                      read(str(location / 'cpu.cfs_period_us'))))
                if limit is not None:
                    budget = min(budget, limit)
            except OSError:
                pass
            if location == root or root not in location.parents:
                break
            location = location.parent
    return max(1, budget)


def configure(template, output, capacity):
    tree = ET.parse(template)
    tree.getroot().find('TCSNum').text = str(capacity)
    output.mkdir(parents=True, exist_ok=True)
    data = ET.tostring(tree.getroot(), encoding='unicode') + '\n'
    header = ('#ifndef SUPPLE_ENCLAVE_CAPACITY_HPP\n#define SUPPLE_ENCLAVE_CAPACITY_HPP\n'
              '#define SUPPLE_TCS_CAPACITY %dULL\n#endif\n' % capacity)
    changed = False
    for name, contents in (('Enclave.config.xml', data), ('EnclaveCapacity.hpp', header)):
        path = output / name
        if not path.exists() or path.read_text() != contents:
            path.write_text(contents)
            changed = True
    stamp = output / 'cpu-config.stamp'
    if changed or not stamp.exists():
        stamp.touch()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configure', type=Path)
    parser.add_argument('--output-dir', type=Path, default=Path('.build'))
    parser.add_argument('--capacity', default='auto')
    args = parser.parse_args()
    capacity = available_cpus() if args.capacity == 'auto' else int(args.capacity)
    if capacity < 1:
        parser.error('capacity must be positive')
    if args.configure:
        configure(args.configure, args.output_dir, capacity)
    else:
        print(capacity)
