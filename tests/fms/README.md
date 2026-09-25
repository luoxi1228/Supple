# FMS v2 回归测试

从仓库根目录执行：

```sh
ASAN_OPTIONS=detect_leaks=0 bash tests/fms/run_tests.sh
```

测试直接编译 `Enclave/SubSample_v2/FMS` 的生产源码。主回归使用宿主端常数时间
OFork 适配器并启用 AddressSanitizer 和 UndefinedBehaviorSanitizer；另一个 smoke
test 直接执行生产 OFork 汇编特化和通用字节回退。测试覆盖任意长度和容量、奇偶
长度、单侧零容量、控制数组非零偏移、规范化填充以及多种记录宽度；小规模用例
验证输出中的准确记录身份，大规模用例覆盖至 1025 条记录。
平衡的 2 的幂长度还对照按层与按连续子块后序执行的结果，并检查离线控制字
重排后，在线 OFork 网络及最终输出排列与原实现逐字节一致。
