# OFRSupple 优化实现与测量口径

本文记录实现、证据来源和解释边界，供最终性能报告引用。审阅日期为
2026-10-02。最终候选是 `stage_06_batch` 的 `batch_no_local`；该阶段的正式
七轮结果由主报告汇总，本文不把三轮短测当作最终验收数字。

## 数据来源及时间边界

- `stage_*/native.csv` 保留两次预热后的七轮原始成对测量，执行顺序逐轮
  交替。`native_metadata.json` 记录源哈希、编译器、宿主和允许的 CPU 集合；
  允许集合不等于固定绑定到一个物理核。`source.tar.gz` / `source.patch`
  是对应阶段的实现依据，不能用当前源文件解释所有旧阶段。
- `comparison.csv` 从原始测量轮计算中位数。`total_ms` 是逐轮总时间的
  中位数，不能用 offline 中位数加 online 中位数替代。在线加速既要报告
  `Supple_online / OFR_online` 的中位数比，也要检查各成对轮的比值。
  `online_2x_all_rounds` 表示每一对测量轮都至少达到 2 倍。
- Native 的 `control_ms` 只计控制构建，`apply_ms` 计应用控制，包括结果
  数组和 online 工作区的分配、清理；确定性 membership、输入记录和
  SWOMark 在其计时范围外。它使用真实 Compact/OFork，却不包括加解密或
  enclave 边界。公开 const 控制入口保留根 membership，根 OFR 需要复制。
- SGX 的 `offline_ms` 包括 SWOMark、控制准备及 offline 工作区/根 membership
  的释放；生成的自有 membership 可原地路由。`online_ms` 计数据路由。
  `total_ms` 即 `ret.ptime = offline + online`，不包含外层解密、输出加密、
  PRB 初始化及在 online 计时前分配结果数组的成本。真正端到端的
  `ecall_ms` 还包含这些成本和 enclave 调用边界。宿主输入加密和输出验证
  位于 ECALL 计时之外；首次测量输出完整检查认证、身份及每个样本内唯一性。
- 主时间、`*_heap.csv` 和 `*_profile.csv` 分开运行，避免分配记账和阶段
  时钟改变主性能数字。Formal profile 默认是单独一轮。短诊断的 native
  `median` 行只重算 control/apply/total，分项 tags/normalize/write 等继承
  最后一轮；它们不是分项中位数。需要分项中位数时，应从 `kind=round`
  的原始行重新汇总。

SIM 构建、签名日志和 `sim_metadata.json` 本身不证明 ECALL 测量成功，必须
有有效的 `sim.csv` / `sim_heap.csv`。Native、SIM 与目标 SGX HW 的数字分开
报告，宿主比值不能当作 HW 提速比例。

用户要求停止测试后，Stage06 SIM 的实际完成范围如下：N=2^20、8 B、
K=16、P=1/16 的七轮主计时、heap 和单独 profiling 已完成，数据分别为
`sim.csv`、`sim_heap.csv`、`sim_profile.csv`；N=2^20、16 B、K=64、P=1/64
的七轮主计时在 `sim_k64.csv` 中完整保存。两个主计时配置均采用两次
预热。`sim_k64_heap.csv` 停止时只有表头，没有有效测量轮，不能报告为
完成的 heap 结果。SIM shapes 未运行，完整 SIM 矩阵也未完成。

原先完整 SIM 主矩阵因单次 ECALL 的加解密时间远高于算法时间而停止，
其 partial CSV、metadata 和状态保存于
`stage_06_batch/diagnostics/sim_matrix_partial/`；不把 partial 标为完整
矩阵，也不用于补齐代表性测试之外的结果。完整七点矩阵已在 Native
测得；stage00–02 缺失的 native heap 不再补测，报告保持
缺失。目标 HW 尚未测得，其完整矩阵仍按下述命令交付，留待用户自行决定
是否在目标机器执行。

## 各阶段实际改变了什么

| 阶段 | 主要变化及尚存成本 |
| --- | --- |
| `stage_00_baseline` | 每门一个 byte；生成 DFS 控制后完整重放 membership；平衡节点随后转换成 online 后序布局。 |
| `stage_01_packed` | 每门 2 bit，四门一个 byte，offset/count 仍是逻辑门数；布局临时数组也压缩。保留完整旧 pipeline，逐门检查/解码一度增加时间。 |
| `stage_02_fused` | 生成控制时立即应用同一门到 membership，删除完整的独立 replay；仍是 stride 翻倍的 DFS 和后续转换。 |
| `stage_03_tiled` | 平衡二次幂节点按公开 stride/residue 分块，两遍统计/分配 rank，直接写 packed 后序控制；删除该路径的索引、控制副本和 feature 大数组。其他形状仍为融合 DFS。 |
| `stage_04_workspace` | 自有 membership 原地路由，标签累加权重并原地 Normalize，按 depth 复用投影/标签/feature、复用计数、提前释放根；加入单 word 标签 SIMD、直接投影与跳过 leaf 投影。该 checkpoint 也包含 4/8/16-residue SIMD writer、窄计数器及早期 stride prefix 优化，不能把全部提速归因于 workspace。 |
| `stage_05_latency` | 固定 n=2/4/8 后序网络去掉递归和 cursor 开销；分类直接使用 pair fields。门顺序与控制值不变。 |
| `stage_06_batch` | unchecked cursor 强制内联；B=8/16 连续独立四门使用 SSE2 一次解码与路由，n=8 只批量处理最后四门。n=4 的四门有依赖，继续顺序执行；其他宽度/尾部保留原 scalar/汇编。不采用 local cursor copy。 |

控制语义始终是 `00→(x,x)`、`01→(x,y)`、`10→(y,x)`、`11→(y,y)`。
所有布局选择、tile/counter 宽度、SIMD/tail 分支及访问位置依赖公开尺寸和
offset，控制值通过算术/SIMD mask 选择源。没有重新定义网络、采样或 leaf
shuffle：两种算法当前都跳过 leaf Shuffle，这也是本次比较的共同边界。
Packed 长度严格为 `ceil(G/4)`；N=2^24、M=N/16、K=16 的 OFR 控制由
720 MiB 降为 180 MiB。SWO 选择位仍单独按 bit 存储。

Native profiling 中 `write_ms` 在融合后包含控制生成与 membership 路由，
`replay_ms` 只剩公开 const 入口必须保留的根复制。ECALL 的自有根不需要
该复制。`prepare_ms` 现在只记录 prepared 节点元数据；它接近零不代表
准备工作消失，主要工作已计入 write。`project_ms` 为需要继续递归的
membership 切片；n=m、k=1 叶节点在读取 membership 之前返回，无需投影。
释放与未单列的分配、bookkeeping 均保留在真实 offline/ECALL 时间中。

## 结果解释与未保留的候选

作为 stage06 前的检查点，正式 stage05 native 主矩阵中，8 B、K=16 的
在线加速约 1.52–1.55 倍；N=2^24 的 offline 从旧 byte 基线 28.873 秒
降至 5.206 秒，但总时间仍是 paired Supple 的 1.213 倍。16 B、K=64
在 N=2^20/22 的总时间比为 1.0008/1.0657。来源为 `comparison.csv` 的
`stage_05_latency,matrix` 行。这些是旧检查点，不能用于宣称最终候选达到
2 倍目标。Stage06 的短测、源码及审计见其 `diagnostics/`，正式矩阵另报。

- Stage04 的 residue group-first pass-two 和 quota-minus-rank 变体没有
  提高成对总时间而回退。早期 prefix 覆盖 stride 8/16 产生回退，只保留
  stride≤4。SSE4/8/16 的初始 checkpoint 被后续综合实现取代，不应把
  candidate_medians 的 `retained=no` 都解释为该 SIMD 技术被放弃。
- Stage06 `inline_only` 短测未显示独立收益；最终 batch 仍保留内联。
  `local_stream` 在 16 B、K=64、N=2^20 的 online 中位数为 153.449 ms，
  对应 inline-only 为 142.527 ms，约慢 7.7%，故回退 local copy。
  `batch + local` 仅编译，没有性能结果，不能声称成功或失败。最终保留
  `batch_no_local`。证据为 stage06 diagnostics 原始 CSV 和说明。
- `CONFIG.h` 仍开启 `COUNT_OSWAPS`，未通过关闭计数取得收益。独立四门
  batch 将四次 TLS 加一合并为一次加四，逻辑门计数和所有控制值保持
  相同。收益可能同时来自 SIMD、解码、减少汇编 clobber/spill 和减少
  TLS 更新；目前没有 counter-off 对照，不把总收益全部归因于 SIMD。

Compact 占主导时，新增 OFR 加速只影响较少节点。正式 stage05 的
compact 形状总体比值约 1.002，接近持平。mixed 的 16 B、N=4096、M=48、
K=128 包含 n=3072 等非二次幂节点，使用通用 DFS fallback，stage05
online 为 Supple 的约 1/0.589，即更慢，总时间约 2.154 倍；不能用平衡
主矩阵概括该形状。相同 mixed 的 256 B 测例总时间比约 0.903，说明
记录宽度会改变门数据移动成本与控制准备成本的比例。最终 batch 只覆盖
8/16 B 的独立连续门，不会自动修复所有 generic DFS 形状。

## heap 数字不是同一种测量

`native_heap.csv` 的 `control_apply_heap_peak_bytes` 是 native 控制/apply
scope 内活跃的请求分配字节峰值；调用者预先生成的 membership 和输入
数组不计入该 scope。持续缓存的 Compact scratch 在后续轮中仍计入 live
峰值，包括在交替算法之间保留的缓存。

SGX `algorithm_heap_peak_bytes` 覆盖完整算法 ECALL，自有解密数组、SWOMark、
控制、plain 结果、PRB 和工作区均在范围内。Native const 入口有 root copy，
ECALL owned 入口没有；两者的分配生命周期不同，不能把这两个峰值直接
做相减归因。两类 live 峰值都不含 allocator metadata、RSS、EPC 或运行时
预留内存；失败记账的结果必须标 invalid，不能当成零。

旧 offline profile 的 `offline_heap_peak_bytes` / `total_heap_peak_bytes`
来自 `g_peak_heap_used`，是 SGX runtime 的进程/实例存活期堆增长高水位。
释放不使它下降，warmup 与前一个算法的增长可能影响后一轮；它也不是
上述 live 请求字节计数。`run_experiments.py` 的 heap 则是签名配置估算：
按公开 tree/word 宽度累计同时保留的 per-depth 容量，再加余量，既不是
测得峰值，也不是 EPC 占用。模型按生产 `SWOFrontier` 的均衡组划分，
Compact 子投影仍预留 parent n 行，且只对需要递归的 fork 子节点分配
投影；multiword/asymmetric/frontier 不按单 word 主矩阵简单线性外推。

## 在目标 HW 上复测

从仓库根目录，在具有 SGX 驱动/设备、SDK 和所需构建依赖的目标机器上
串行运行以下命令。runner 在 `/tmp` 恢复源、构建、签名，生产配置和
已有结果保持原状；输出目录必须是尚未测过的新目录。正式阶段归档后
再执行最终候选命令。默认 signed heap 为 4 GiB，可用 `--heap` 依据目标
实例的配置预算明确调整；该值不是实测 live heap。

```sh
python3 tests/ofrsupple/run_optimization_stage.py --stage baseline_hw \
  --from-stage RESULTS/ofr_optimization_20261001/stage_00_baseline \
  --backend hw --matrix --rounds 7 --warmup 2 \
  --output RESULTS/ofr_hw_baseline_new

python3 tests/ofrsupple/run_optimization_stage.py --stage batch_hw \
  --from-stage RESULTS/ofr_optimization_20261001/stage_06_batch \
  --backend hw --matrix --rounds 7 --warmup 2 \
  --output RESULTS/ofr_hw_batch_new
```

同时核对 online 各成对轮、offline+online、包含加解密的 ECALL 中位数、
有效 heap 轮数，以及 shape companions 中 compact/mixed 的边界。两次
运行必须使用同机、同配置、相同 harness；机器上的其他高负载工作应在
测量之外。HW 尚未测得的字段保持缺失，不能用 native/SIM 数字补齐。

实现和 harness 对照入口：[benchmark README](../../tests/ofrsupple/README.md)、
[native harness](../../tests/ofrsupple/benchmark_vs_swo.cpp)、
[ECALL harness](../../tests/ofrsupple/paired_sgx_benchmark.cpp)、
[isolated runner](../../tests/ofrsupple/run_optimization_stage.py)。
