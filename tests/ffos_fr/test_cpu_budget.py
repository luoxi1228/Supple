import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import tempfile
import subprocess
import shutil
import unittest
from unittest import mock
from cpu_budget import available_cpus, quota_budget, configure
import xml.etree.ElementTree as ET

class CpuBudgetTest(unittest.TestCase):
    def probe(self, cpus, files):
        def read(path):
            if path not in files: raise FileNotFoundError(path)
            return files[path]
        return available_cpus(affinity=cpus, read=read)
    def test_affinity_16_32_64_and_fallback(self):
        for n in (1, 3, 16, 32, 64): self.assertEqual(self.probe(n, {}), n)
        with mock.patch('os.sched_getaffinity', side_effect=OSError), mock.patch('os.sysconf', return_value=32):
            self.assertEqual(self.probe(None, {}), 32)
        self.assertEqual(available_cpus(affinity=0, online=0, read=lambda _: ''), 1)
    def test_quota_ceiling_and_affinity(self):
        for quota, wanted in (('150000 100000',2), ('max 100000',64), ('-1 100000',64), ('x 1',64), ('0 0',64)):
            self.assertEqual(self.probe(64, {'/sys/fs/cgroup/cpu.max':quota}), wanted)
        self.assertEqual(quota_budget('900000','100000'),9)
    def test_unified_ancestors_and_v1(self):
        files = {'/proc/self/cgroup':'0::/a/b\n', '/proc/self/mountinfo':'1 0 0:1 / /cg rw - cgroup2 cgroup rw\n',
                 '/cg/a/b/cpu.max':'max 100000', '/cg/a/cpu.max':'250000 100000','/cg/cpu.max':'800000 100000'}
        self.assertEqual(self.probe(64, files),3)
        files = {'/proc/self/cgroup':'1:cpu,cpuacct:/a\n', '/proc/self/mountinfo':'1 0 0:1 / /cg rw - cgroup cgroup rw,cpu,cpuacct\n',
                 '/cg/a/cpu.cfs_quota_us':'1700000','/cg/a/cpu.cfs_period_us':'100000'}
        self.assertEqual(self.probe(64,files),17)
    def test_scaling_budgets_include_last_non_power_of_two(self):
        from run_parallel_benchmark import budgets
        self.assertEqual(budgets(1), [1])
        self.assertEqual(budgets(3), [1,2,3])
        self.assertEqual(budgets(17), [1,2,4,8,16,17])
        self.assertEqual(budgets(64), [1,2,4,8,16,32,64])
    def test_make_capacity_change_rebuilds_in_same_invocation(self):
        source=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'Enclave').mkdir()
            shutil.copy2(source/'cpu_budget.py',root/'cpu_budget.py')
            shutil.copy2(source/'Enclave/Enclave.config.xml',root/'Enclave/Enclave.config.xml')
            (root/'probe.mk').write_text('probe: .build/EnclaveCapacity.hpp .build/Enclave.config.xml\n\t@cat $^ > $@\n')
            for n in (1,16,32,64):
                command=['make','-f',str(source/'Makefile'),'-f','probe.mk','probe',f'CPU_CAPACITY={n}']
                subprocess.run(command,cwd=root,check=True,capture_output=True)
                self.assertIn(f'{n}ULL',(root/'probe').read_text())
                self.assertIn(f'<TCSNum>{n}</TCSNum>',(root/'probe').read_text())
                before=(root/'probe').stat().st_mtime_ns
                subprocess.run(command,cwd=root,check=True,capture_output=True)
                self.assertEqual((root/'probe').stat().st_mtime_ns,before)
    def test_config_same_capacity_and_no_rewrite(self):
        with tempfile.TemporaryDirectory() as root:
            out=Path(root)
            template=Path(__file__).resolve().parents[2]/'Enclave/Enclave.config.xml'
            for n in (16,32,64):
                configure(template,out,n)
                self.assertEqual(int(ET.parse(out/'Enclave.config.xml').find('TCSNum').text),n)
                self.assertIn(f'{n}ULL',(out/'EnclaveCapacity.hpp').read_text())
                before=(out/'cpu-config.stamp').stat().st_mtime_ns
                configure(template,out,n)
                self.assertEqual((out/'cpu-config.stamp').stat().st_mtime_ns,before)
if __name__ == '__main__': unittest.main()
