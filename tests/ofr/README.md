# OFR v2 回归测试

从仓库根目录执行：

```sh
ASAN_OPTIONS=detect_leaks=0 bash tests/ofr/run_tests.sh
```

测试直接编译 `Enclave/SubSample_v2/OFR` 的生产源码。主回归使用宿主端常数时间
OFork 适配器并启用 AddressSanitizer 和 UndefinedBehaviorSanitizer；另一个 smoke
test 直接执行生产 OFork 汇编特化和通用字节回退。测试覆盖任意长度和容量、奇偶
长度、单侧零容量、控制数组非零偏移、规范化填充以及多种记录宽度；小规模用例
验证输出中的准确记录身份，大规模用例覆盖至 1025 条记录。
平衡的 2 的幂长度还对照按层与按连续子块后序执行的结果，并检查离线控制字
转换后，在线 OFork 网络输出与直接递归执行逐字节一致。
左输出占前 `n_left` 条记录，右输出紧随其后；各侧内部顺序按网络原位布局。

OFRCompact 分阶段宿主基准：

```sh
bash tests/ofr/run_phase_benchmark.sh 65536 9
```

第三个可选参数限制最大记录宽度，例如大型输入可运行
`bash tests/ofr/run_phase_benchmark.sh 1048576 5 64`。

`control_ms` 从已规范化的标签生成 DFS 控制字，包含公开入口校验；
`postorder_ms` 是单独的 DFS 到后序转换时间；
`prepared_control_ms` 从原始标签生成后序控制字，包含规范化，按公开规模选择生成方式；
`offline_total_ms` 使用此准备路径。
各 `online_ms` 使用生产 OFork 汇编，计入完整的原位网络执行。
这是宿主基准；SGX 内绝对耗时需另行测量。
