# OFR v2 回归测试

从仓库根目录执行：

```sh
ASAN_OPTIONS=detect_leaks=0 bash tests/ofr/run_tests.sh
```

测试直接编译 `Enclave/SubSample_v2/OFR` 的生产源码，构建产物保存在独立的
`/tmp` 目录。主回归使用宿主端常数时间 OFork 适配器并启用 AddressSanitizer
和 UndefinedBehaviorSanitizer；另一个 O2 smoke test 执行生产 SSE2/汇编路径
和通用字节回退。8 B/16 B 后序网络中的四个独立门使用
`FourGateSSE2.hpp`，其他记录宽度和存在门间依赖的子网络保留原汇编路径。
SSE2 使用 GNU vector/builtin 和 `memcpy`，兼容 SGX SDK 的 `-nostdinc` 构建；
四门路径仅在公开的 x86-64/SSE2 配置下启用，宿主测试不会构建或签名 enclave。

全部持久门控制使用 `ofr::PackedControls`：第 `g` 个门占第 `g/4` 个 byte 的
`2*(g%4)` 起始两 bit。`size()` 是逻辑门数，`byte_size()` 严格为
`ceil(size()/4)`；节点 offset、writer 返回位置及 view 的 offset/count 都以门
为单位。`view(offset, count)` 和 `mutable_view(offset, count)` 提供有边界的
逻辑 span，保留同一 byte 内相邻节点的控制及末 byte 的零 padding。在线直接
从 packed span 读取控制，不展开完整 byte 控制数组。

当前固定回归覆盖以下内容，计数与 Stage 06 的实际输出对应：

- 250,952 个准确路由及 28,866 个规范化/容量用例，包含奇数长度、任意左右
  容量、单侧零容量和非零门偏移；检查记录身份及完整记录内容。
  `OFRNormalizeInPlace` 和自有标签规范化路径逐标签对照原 API 与独立填充参考。
- Packed 类型覆盖全部四种控制值、零门、末 byte padding、共享 byte、重叠
  拷贝、前后哨兵、短 span 和 `SIZE_MAX` 越界。2/4/8/16 门的 typed encoded
  writer 分别检查 128、32,768、524,288、33,328 次成功写入，共 590,512 次；
  两门 writer 另拒绝 1,920 次非法编码。四种绝对 bit-slot 对齐均有覆盖。
- Cursor 在门偏移 0～7、长度 0～17、每个起始位置上混合 checked、unchecked
  和四门解码，共 8,208 次；耗尽后的 checked 读取抛错，源数据与 padding 不变。
  公开入口在 unchecked 执行前验证完整控制 span，截断末段不会修改输出。
- 38,016 个融合 DFS 用例覆盖 `N=1..33` 的任意容量、精确/欠填标签和
  1/2/3/9 word membership。控制、最终标签和每个 membership word 均对照
  独立 DFS 生成加独立 replay，并检查偏移 0～7 的邻接哨兵。
- 1,040 个分块后序用例覆盖平衡 `N=2..8192`、1/2/3/8/17 word membership、
  tile 边界和偏移 0～7。控制逐门对照独立 DFS 转后序，最终标签和 membership
  逐 word 对照独立 replay；独立 DFS、按层、后序接口的布局语义分别验证。
- 四个窄计数极值用例覆盖 `N=4096` 的 stride 16/length 256 和 `N=524288`
  的 stride 8/length 65536。LEFT/RIGHT 与 BOTH/ZERO 两类块状输入分别达到
  最大 count/rank 128 和 32,768，控制、最终标签、membership 全量对照参考。
- 三个公开 membership 入口各有两例 byte-product 溢出：`N*words` 可表示、
  再乘 `sizeof(size_t)` 溢出时，在写入标签、控制或 rows 前拒绝。公开分派还
  保留 64-bit membership SIMD 和动态汇编 `UINT32_MAX` 宽度边界；宿主回归
  运行于 x86-64，SDK 构建检查另行执行。
- 生产 SSE2/汇编测试有 131,072 个独立四门包用例：`N=8`，前八门 straight，
  最后四门遍历全部 256 编码，8 B/16 B、门偏移 0～7、数据 byte 偏移 0～31。
  另有 4,096 个 `N=16/32` 混合后序用例，对照独立逐门 C++ DFS reference。
  检查完整记录、前后数据哨兵和控制/padding 不变；启用 `COUNT_OSWAPS` 时，
  每次生产执行的计数增量分别为 12/32/80。现有单门控制和 4/7/8/12/16/24/
  32/40/64 B 宽度回归继续保留。

左输出占前 `n_left` 条记录，右输出紧随其后；各侧内部顺序按网络原位布局。

`run_tests.sh` 的生产 SSE2/汇编 smoke test 使用 O2。Stage 06 还单独运行了
同一 fixture 的 ASan/UBSan 版本，日志保存在
`RESULTS/ofr_optimization_20261001/stage_06_batch/assembly_smoke_asan_regression.log`。
需要复查这一路径时，可额外在隔离目录编译运行，无需重复加入常规测试脚本：

```sh
test_build=$(mktemp -d /tmp/supple-ofr-asm-asan.XXXXXX)
"${CXX:-g++}" -std=c++11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -IEnclave -I"${SGX_SDK:-/opt/intel/sgxsdk}/include" \
  tests/ofr/assembly_smoke.cpp Enclave/SubSample_v2/OFR/OFR.cpp \
  Enclave/SubSample_v2/OFR/helper.cpp -o "$test_build/assembly_smoke_asan"
ASAN_OPTIONS=detect_leaks=0 "$test_build/assembly_smoke_asan"
```

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
各 `online_ms` 使用生产 SSE2/汇编路径，计入完整的原位网络执行。
这个独立 phase 基准先预热三次，再分别取各阶段的测量中位数；阶段中位数之和
不等于同轮总耗时的中位数。它不同于 OFRSupple 成对基准的两次预热/七次测量。
这是宿主基准；SGX 内绝对耗时需另行测量。SIM 可验证 enclave 接口与模拟环境
中的行为，其耗时不代表 HW 的 EPC、分页或真实 SGX 运行成本。
