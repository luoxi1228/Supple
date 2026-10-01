# OFRSupple 最终 Native 结果

最终候选为 `stage_06_batch / batch_no_local`。在七个主矩阵配置上，
online 的中位数加速为 **2.087–2.898 倍**，每个配置的七轮成对测量均
超过 2 倍；最小单轮配对加速为 2.039 倍。六个配置的
offline+online 低于同轮 Supple，8 B、N=2²⁴ 的总耗时仍高 **7.31%**。
因此主矩阵达到了新的 online 目标，但原先“全部规模总耗时低于 Supple”
的目标尚未全部达到，特殊形状也存在明确限制。

本文仅汇总 Native 正式结果。SGX SIM 已完成两个 N=2²⁰ 代表性配置的
七轮主计时，以及 8 B、K=16 配置的 heap/profile。16 B、K=64 的 heap
没有有效测量轮，SIM shapes 未运行，完整 SIM 矩阵没有完成。用户要求
停止测试后不再补测；目标 SGX HW 尚未实测，不能用这里的比值替代 HW
结论。已有 SIM 数据由主报告单列。

## 测量和数据来源

全部表格取自 [comparison.csv](comparison.csv) 的 `backend=native` 行，
该文件由原始 `kind=round` 数据汇总。每点两次预热、七次测量，Supple 与
OFRSupple 的执行顺序逐轮交替，实验串行运行。最终源归档与元数据位于
[stage_06_batch](stage_06_batch/)，元数据记录 Intel Xeon Platinum 8369B、
g++ 13.3.0 和源哈希 `04d765eb777ae5c9bcc4c8f670329761d3fe8847ddec5ae0e99179b47909d581`。
CPU 允许集合为 0–7，不代表固定绑定到一个物理核。

Native offline 是控制构建，online 是控制应用及其结果/工作区分配和清理；
预先生成的 membership、输入数据、SWOMark、加解密及 enclave 边界不在
Native 时间内。表中的“合计”是各轮实际合计的中位数，不是两个分项
中位数相加。具体时间、分配和 ECALL 边界见
[implementation_notes.md](implementation_notes.md#数据来源及时间边界)。

## 最终主矩阵

时间单位为 ms；每个时间格按 **Supple / OFRSupple** 排列。
总耗时比为 OFRSupple/Supple，低于 1 表示 OFRSupple 更快。
Online 加速是两个 online 中位数的比；最小配对加速直接检查七对原始轮次。

| 记录 / K / P | N | Offline | Online | 合计 | 总耗时比 | Online 加速 | 最小配对加速 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 B / 16 / 1⁄16 | 2¹⁶ | 7.855 / 9.304 | 6.929 / 2.449 | 14.784 / 11.743 | 0.794 | 2.830× | 2.798× |
| 8 B / 16 / 1⁄16 | 2¹⁸ | 34.326 / 44.453 | 31.129 / 10.818 | 65.447 / 55.271 | 0.845 | 2.877× | 2.868× |
| 8 B / 16 / 1⁄16 | 2²⁰ | 150.714 / 211.505 | 140.977 / 48.651 | 294.846 / 259.962 | 0.882 | 2.898× | 2.853× |
| 8 B / 16 / 1⁄16 | 2²² | 668.924 / 1038.499 | 623.129 / 217.526 | 1291.844 / 1257.033 | 0.973 | 2.865× | 2.767× |
| 8 B / 16 / 1⁄16 | 2²⁴ | 2996.607 / 5242.680 | 2777.676 / 958.391 | 5779.086 / 6201.387 | **1.073** | 2.898× | 2.825× |
| 16 B / 64 / 1⁄64 | 2²⁰ | 210.232 / 287.506 | 224.540 / 102.617 | 435.275 / 390.094 | 0.896 | 2.188× | 2.168× |
| 16 B / 64 / 1⁄64 | 2²² | 931.507 / 1399.791 | 1012.558 / 485.229 | 1944.949 / 1884.334 | 0.969 | 2.087× | 2.039× |

对应原始时间为 [native.csv](stage_06_batch/native.csv)，另行记账的分配峰值
为 [native_heap.csv](stage_06_batch/native_heap.csv)。Native 的“合计”仍是
算法范围时间，完整加解密 ECALL 时间必须查 SGX 测量。

## 阶段演进和控制占用

以下均为 8 B、K=16、P=1⁄16 的正式阶段结果，时间单位 ms。
“控制 MiB”和“Native heap MiB”依次列出 N=2²⁰ / N=2²⁴；
1 MiB=2²⁰ bytes。各阶段保持各自原始 Supple/OFRSupple 配对，未拼接
不同阶段的分项来构造一个不存在的测量轮。

| 阶段 | N=2²⁰ Offline | N=2²⁰ 合计 | N=2²⁴ Offline | N=2²⁴ 合计 | 控制 MiB | Native heap MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 00 byte baseline | 724.868 | 816.011 | 28873.443 | 30703.278 | 37 / 720 | — / — |
| 01 packed | 872.596 | 996.385 | 27551.381 | 30043.910 | **9.25 / 180** | — / — |
| 02 fused | 816.372 | 932.057 | 29094.062 | 31446.583 | 9.25 / 180 | — / — |
| 03 tiled | 565.129 | 685.344 | 11505.093 | 13905.000 | 9.25 / 180 | 33.251 / 564.001 |
| 04 workspace + writer SIMD | 217.825 | 320.364 | 5436.835 | 7412.595 | 9.25 / 180 | 34.127 / 578.002 |
| 05 latency | 209.074 | 301.140 | 5205.627 | 6987.361 | 9.25 / 180 | 34.127 / 578.002 |
| 06 batch | 211.505 | 259.962 | 5242.680 | 6201.387 | 9.25 / 180 | 34.127 / 578.002 |

Stage00–02 的 heap companions 缺失，按用户要求不再补测，空格不代表
零或推算值。压缩后持久 OFR 控制严格为 `ceil(G/4)` bytes，逻辑门数、offset
和四种控制语义保持不变。N=2²⁴ 的 720 MiB 确实降为 180 MiB；全部
布局临时控制也使用 packed 表示，没有 online 全量展开 byte 数组。

Packed 是必须保留的空间改动，单独压缩后的解码曾增加时间；融合阶段
也未在所有规模独立改善总时间，不能把七个 checkpoint 描述为每一步
都必然下降。直接分块后序生成消除了平衡路径的布局转换、索引和控制
副本。Stage04 同时包含 workspace、单 word 标签 SIMD、4/8/16 residue
writer、窄计数和早期 stride prefix，整段降幅不能全部归因于 workspace。
按 depth 保留容量使 heap 峰值略高于 stage03，同时减少了反复分配；
实际 Native 峰值保留在表中。

最终 offline 相对 byte baseline 在 N=2²⁰ 从 724.868 降至 211.505 ms，
N=2²⁴ 从 28.873 降至 5.243 秒。最后的四门 batch 主要改善 online，
offline 的小幅差异不能视为该 batch 对控制构建有独立收益。

## Offline 仍然花在哪里

正式独立 [native_profile.csv](stage_06_batch/native_profile.csv) 是
N=2²⁰、8 B、K=16 的单独一轮 profiling；不将这一轮分项写成七轮中位数，
也不把它的 online 时间与主矩阵合并。

| 分项 | ms | 实际工作 |
| --- | ---: | --- |
| tags | 5.295 | 读取 membership，生成标签并累计左右权重 |
| normalize | 7.437 | 对自有标签原地平衡配额 |
| write | 183.832 | 统计/rank、生成门控制并立即路由 membership |
| replay | 1.475 | Native const 入口保留调用者根 membership 所需的复制 |
| project | 8.174 | 生成需要继续递归的子 membership |
| prepare | 0.006 | 记录公开 prepared 节点元数据 |
| 实际 offline | **209.550** | 包含未单列的分配、释放和 bookkeeping |

控制生成与 membership 路由仍占该轮 offline 的约 **87.7%**，是剩余
主要成本。独立 replay 和后序转换已经消除；`prepare` 接近零表示这些
工作已融合进 write，不表示控制准备整体免费。ECALL 的自有根可直接
路由，因此没有 Native const 入口这一份根复制。

## 特殊形状：未达到 2 倍的配置

下表列出最终七个 shape companions 中未达到 2 倍的全部五个配置。
N 均为 4096，时间单位 ms，时间格仍按 Supple / OFRSupple 排列。

| 形状 | M / K / 记录 | Online | Online 加速 | 最小配对加速 | 合计 | 总耗时比 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| compact | 16 / 16 / 16 B | 0.110 / 0.104 | 1.060× | 1.003× | 0.230 / 0.231 | 1.007 |
| mixed | 48 / 128 / 16 B | 0.864 / 1.428 | **0.605×** | 0.601× | 1.793 / 3.794 | **2.115** |
| equal_wide | 64 / 64 / 64 B | 1.647 / 1.030 | 1.599× | 1.589× | 2.213 / 1.661 | 0.751 |
| equal_256 | 64 / 64 / 256 B | 6.143 / 3.665 | 1.676× | 1.670× | 6.701 / 4.298 | 0.641 |
| mixed_256 | 48 / 128 / 256 B | 9.661 / 7.127 | 1.355× | 1.347× | 10.601 / 9.497 | 0.896 |

另外两个配置 `equal`（M=64、K=64、16 B）和 `frontier`
（M=64、K=128、16 B）每轮均达 2 倍，online 中位数加速分别为
2.333 / 2.132 倍，最小配对加速为 2.311 / 2.097 倍。原始形状数据见
[native_shapes.csv](stage_06_batch/native_shapes.csv)。

Compact 主导时，能由 OFR batch 加速的工作占比很小。Mixed 中存在
n=3072 等非二次幂节点，仍使用通用融合 DFS；其 16 B 配置是明确回退，
不能用平衡主矩阵掩盖。最终四门 SSE2 batch 仅特化 8/16 B，64/256 B
继续使用原宽度路径，所以更宽记录也未达到相同倍数。16 B 与 256 B
mixed 的结果不同，说明数据移动成本与控制准备成本的比例会随宽度变化。

## 正确性、候选选择和内存边界

最终候选回归通过，结果可直接核对：

- [OFR 日志](stage_06_batch/ofr_regression_final.log)：250952 次精确路由、
  28866 次 Normalize/容量检查、38016 次融合多 word 路由、1040 次分块
  后序比较，以及窄计数/容量溢出边界；131072 个生产四门 packet 和
  4096 个 mixed 后序/DFS case。
- [OFRSupple 日志](stage_06_batch/ofrsupple_regression_final.log)：1843 个
  集成 case、2304 个单 word 标签 case、10 个损坏 plan 在任何数据写入前
  被拒绝。覆盖 K=65/129、多 word、最高位、奇数/不对称与 raw offset。
- [生产汇编 smoke 日志](stage_06_batch/assembly_smoke_asan_regression.log)：
  四门所有控制组合、非对齐记录/packed span 和首尾哨兵。
- [内存/CSV 日志](stage_06_batch/memory_regression_final.log)：请求分配
  记账、失败记录拒绝和 CSV schema 回归。

静态实现保持公开尺寸决定地址、循环、布局和 SIMD/tail 分支，秘密控制
通过算术/SIMD mask 选择源；批处理只用于四个独立门，n=4 的依赖门仍
按原顺序执行。`COUNT_OSWAPS` 保持开启，每组四门由四次 TLS 加一变为
一次加四，逻辑计数等价。没有关闭计数的对照，因此最终收益可能同时
来自 SIMD、解码、减少 spill 和减少 TLS 更新，不能全部归因于 SIMD。
放弃的 local cursor copy、早期 prefix/group-first 等候选及其证据见
[实现说明](implementation_notes.md#结果解释与未保留的候选) 和
[stage06 诊断审计](stage_06_batch/diagnostics/audit.md)。

Native heap 是控制/apply scope 内活跃请求字节的峰值，输入数据和提前
生成的 membership 在 scope 外；SGX 算法 heap 包含完整 ECALL 内存生命周期，
两者不能直接作差。它们也都不是 RSS、EPC 或签名 heap 配置。旧
`g_peak_heap_used` 是实例堆增长高水位，不会随释放下降，不能替代 live
峰值。详细定义见 [heap 说明](implementation_notes.md#heap-数字不是同一种测量)。

目标 HW 使用相同归档、两次预热和七轮串行成对测量，命令在
[HW 复测说明](implementation_notes.md#在目标-hw-上复测)。应同时检查
offline+online、包含加解密的 ECALL、有效 heap 轮数和 mixed/compact；
本报告不填补尚未测得的 HW 数据。
