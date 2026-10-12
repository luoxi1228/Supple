#!/usr/bin/env python3
"""Build once in /tmp; measure Par scaling against its one-thread SIMD baseline."""
import argparse
import csv
import fcntl
import json
import os
import platform
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from cpu_budget import available_cpus
import run_experiments as experiments
from run_optimization_stage import snapshot, build_sgx, run


def budgets(available):
    result, value = [], 1
    while value < available:
        result.append(value)
        value *= 2
    return result + [available]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=['native', 'sim', 'hw'], default='native')
    parser.add_argument('--threads', default='auto', help='auto scaling series, or comma-separated budgets')
    parser.add_argument('--case', type=int, nargs=4, default=[65536,1024,64,16])
    parser.add_argument('--heap', type=lambda x: int(x,0), help='override automatic enclave heap estimate')
    parser.add_argument('--rounds', type=int, default=7)
    parser.add_argument('--warmup', type=int, default=2)
    parser.add_argument('--memory', action='store_true')
    parser.add_argument('--profile', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    available = available_cpus()
    threads = budgets(available) if args.threads == 'auto' else [int(t) for t in args.threads.split(',')]
    if not threads or min(threads) < 1 or max(threads) > available or args.rounds < 1 or args.warmup < 0:
        parser.error(f'budgets must be in 1..{available}; rounds positive; warmup nonnegative')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (output/'FFOS_FR_Par.csv').exists(): parser.error('output already exists')
    root = Path(__file__).resolve().parents[2]
    build = Path(tempfile.mkdtemp(prefix='supple-par-scaling.'))
    digest = snapshot(root, build)
    experiments.ENCLAVE_CONFIG = build/'Enclave/Enclave.config.xml'
    n,m,k,width=args.case
    heap=args.heap or experiments.estimate_heap(6,n,width,m/n,k,threads=max(threads))
    stack=available*int(ET.parse(experiments.ENCLAVE_CONFIG).find('StackMaxSize').text,0)
    (output/'metadata.json').write_text(json.dumps(dict(build=str(build), source_sha256=digest,
        backend=args.backend, available_cpus=available, budgets=threads, case=args.case,
        host=platform.platform(), cpu_model=next((x.split(":",1)[1].strip() for x in Path("/proc/cpuinfo").read_text().splitlines() if x.startswith("model name")),"unknown"),
        compiler=subprocess.check_output(["g++","--version"],text=True).splitlines()[0],
        heap_bytes=heap, stack_reservation_bytes=stack,
        affinity=sorted(os.sched_getaffinity(0)), rounds=args.rounds, warmup=args.warmup),indent=2)+'\n')
    print(f'Isolated build: {build}; budgets: {threads}', flush=True)
    lock = open("/tmp/supple-ofr-performance.lock", "w")
    fcntl.flock(lock, fcntl.LOCK_EX)
    if args.backend == 'native':
        binary=build/'benchmark'
        run(['bash','tests/ffos_fr/run_benchmark.sh'],build,output/'build.log',
            dict(os.environ,BENCH_BUILD_ONLY='1',BENCH_BINARY=str(binary)))
        base=[binary,str(args.rounds)]
    else:
        base=build_sgx(build,output,args.backend,heap)
        base+=['--rounds',str(args.rounds)]
    rows=[]
    for budget in threads:
        log=output/f'threads_{budget}.csv'
        command=base+['--warmup',str(args.warmup),'--threads',str(budget),'--case',*[str(x) for x in args.case]]
        if args.memory: command+=['--memory']
        if args.profile: command+=['--profile']
        run(command,build,log)
        with log.open() as source: rows.extend(row for row in csv.DictReader(source) if row['method']=='FFOS_FR_Par')
        print(f'Completed budget {budget}',flush=True)
    lock.close()
    with (output/'FFOS_FR_Par.csv').open('w') as target:
        writer=csv.DictWriter(target,fieldnames=list(rows[0]))
        writer.writeheader();writer.writerows(rows)

if __name__ == '__main__': main()
