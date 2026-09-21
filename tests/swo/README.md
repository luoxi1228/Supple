# SWO 回归测试

从仓库根目录执行。默认只测试串行实现：

```sh
ASAN_OPTIONS=detect_leaks=0 bash tests/swo/run_tests.sh serial
```

需要 C++11 编译器、NASM 和 SGX SDK 头文件，可用 `CXX`、`SGX_SDK` 指定路径。
启用 AddressSanitizer 和 UndefinedBehaviorSanitizer；当前执行环境不支持
LeakSanitizer，因此上述命令仅关闭泄漏检查。产物保存在脚本打印的临时目录。

串行测试包含 2,951 组路由用例，验证准确的样本身份和记录内容、输入不变性、
逐位控制流、Compact 次数、非零 bit 偏移及周围保护位、等容量分支、单样本扩展、
最多 257 位 membership 和 4–64 字节记录。另验证 SWOMark、SWOSample 和
DecSuppleSWO。控制流参照实现先 Compact 完整 membership 再投影，独立于生产
代码的预投影优化。宿主测试使用真实 Compact/交换汇编；RNG、加解密和 Shuffle
使用测试适配器，不作为随机性或密码学验证。

`parallel` / `all` 参数保留迁移前后对照测试，但当前收尾范围不含并行版。
已有 18 组宿主对照的迁移前摘要为 `a994cd2e903d06c2`。

## 真实 SGX 仿真：串行 ECALL

`sgx_smoke.cpp` 使用生成的 ECALL 桥、真实 AES-GCM、PRB 和 Shuffle，验证解密后
每个样本的身份、记录内容和样本内无重复。覆盖 `(n,m,k)`：
`(9,3,3)`、`(8,2,8)`、`(8,8,3)`、`(9,2,1)`、`(65,8,129)`。
默认只执行串行；并行入口需显式传入 `--parallel`，其仿真验收暂未完成。

当前 SDK 的 SIM 模式下，默认 PIE/TLS 构建会直接访问宿主 TLS，导致崩溃。
`sgx_smoke.mk` 仅为测试改用 PIC、动态 TLS 和共享对象链接；正式 Makefile 不变。
默认参数的 Enclave 编译及联合链接已通过；使用该测试覆盖配置后，五组串行
用例和 Enclave 销毁均通过。

以下命令会重编译仓库中的构建产物；切回正式构建也须强制重编译，避免混用对象。

```sh
export SGX_SDK=/opt/intel/sgxsdk
export LD_LIBRARY_PATH="$SGX_SDK/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
make -f tests/swo/sgx_smoke.mk -B -j4 enclave.so
make SGX_MODE=SIM untrusted
swo_build=$(mktemp -d /tmp/supple-swo-smoke.XXXXXX)
"$SGX_SDK/bin/x64/sgx_sign" sign \
  -key Enclave/Enclave_private.pem -enclave enclave.so \
  -out "$swo_build/enclave.signed.so" -config tests/swo/sgx_smoke.config.xml
g++ -std=c++11 -O1 -g -I"$SGX_SDK/include" -IEnclave -IUntrusted \
  tests/swo/sgx_smoke.cpp Application/gcm.cpp \
  -L. -L"$SGX_SDK/lib64" -lOSort -lsgx_urts_sim -lcrypto -pthread \
  -Wl,-rpath,"$PWD" -Wl,-rpath,"$SGX_SDK/lib64" -o "$swo_build/sgx_smoke"
"$swo_build/sgx_smoke" "$swo_build/enclave.signed.so"
```
