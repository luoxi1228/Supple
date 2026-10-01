// Paired ECALL benchmark. Encryption of inputs and output verification are
// outside the timed region; enclave decryption/encryption remain inside it.
#include <sgx_urts.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../Untrusted/Enclave_u.h"
#include "../../Application/gcm.h"

namespace {
void Require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
struct Enclave {
  sgx_enclave_id_t id = 0;
  unsigned char input_key[16] = {1}, output_key[16] = {2};
  explicit Enclave(const char *path) {
    sgx_launch_token_t token{};
    int updated = 0;
    const auto status = sgx_create_enclave(path, SGX_DEBUG_FLAG, &token,
                                          &updated, &id, nullptr);
    if (status != SGX_SUCCESS)
      std::fprintf(stderr, "sgx_create_enclave: 0x%x\n", status);
    Require(status == SGX_SUCCESS, "Enclave creation failed");
    Require(Enclave_loadTestKeys(id,input_key,output_key) == SGX_SUCCESS,
            "Loading test keys failed");
  }
  ~Enclave() { if (id) sgx_destroy_enclave(id); }
};
void Verify(const std::vector<unsigned char> &output, size_t n, size_t m,
            size_t k, size_t width, unsigned char *key) {
  const size_t encrypted_width = width + 28;
  std::vector<unsigned char> record(width);
  std::vector<uint32_t> ids(m);
  for (size_t sample=0; sample<k; ++sample) {
    for (size_t i=0; i<m; ++i) {
      const unsigned char *row = output.data()+(sample*m+i)*encrypted_width;
      Require(gcm_decrypt(const_cast<unsigned char *>(row+12),width,nullptr,0,
                          const_cast<unsigned char *>(row+12+width),key,
                          const_cast<unsigned char *>(row),12,record.data()) ==
                  static_cast<int>(width), "Output authentication failed");
      uint32_t id;
      std::memcpy(&id,record.data(),sizeof(id));
      Require(id<n,"Invalid output identity");
      for (size_t b=sizeof(id); b<width; ++b)
        Require(record[b] == static_cast<unsigned char>(id*19),
                "Output payload mismatch");
      ids[i]=id;
    }
    std::sort(ids.begin(),ids.end());
    Require(std::adjacent_find(ids.begin(),ids.end()) == ids.end(),
            "Duplicate record within sample");
  }
}
void Run(const char *path, const char *name, size_t n, size_t m, size_t k,
         size_t width, size_t rounds, size_t warmup, bool memory, bool profile) {
  Require(n && n<=UINT32_MAX && m && m<=n && k && width>=4 &&
          width<=static_cast<size_t>(INT32_MAX) && k<=SIZE_MAX/m &&
          width<=SIZE_MAX-28 && n<=SIZE_MAX/(width+28) &&
          m*k<=SIZE_MAX/(width+28), "Invalid benchmark dimensions");
  Enclave enclave(path);
  const size_t encrypted_width=width+28;
  std::vector<unsigned char> input(n*encrypted_width),output(m*k*encrypted_width);
  std::vector<unsigned char> record(width);
  for (size_t i=0;i<n;++i) {
    std::fill(record.begin(),record.end(),static_cast<unsigned char>(i*19));
    const uint32_t id=static_cast<uint32_t>(i);
    std::memcpy(record.data(),&id,sizeof(id));
    unsigned char *row=input.data()+i*encrypted_width;
    std::fill(row,row+12,0);
    const uint64_t nonce=i;
    std::memcpy(row,&nonce,sizeof(nonce));
    Require(gcm_encrypt(record.data(),width,nullptr,0,enclave.input_key,row,12,
                        row+12,row+12+width)==static_cast<int>(width),
            "Input encryption failed");
  }
  for (size_t round=0;round<warmup+rounds;++round) {
    for (size_t j=0;j<2;++j) {
      const size_t method=(round+j)%2;
      enc_ret ret{};
      ret.collect_memory_profile=memory;
      ret.collect_offline_profile=profile;
      ret.collect_online_profile=profile;
      const auto start=std::chrono::steady_clock::now();
      const auto status=method ?
        DecOFRSupple(enclave.id,input.data(),n,m,k,encrypted_width,output.data(),&ret) :
        DecSuppleSWO(enclave.id,input.data(),n,m,k,encrypted_width,output.data(),&ret);
      const double elapsed=std::chrono::duration<double,std::milli>(
          std::chrono::steady_clock::now()-start).count();
      Require(status==SGX_SUCCESS,"ECALL failed");
      Require(!memory || ret.memory_profile_status==MEMORY_PROFILE_VALID,
              "Invalid enclave heap measurement");
      Require(ret.ptime>0 && std::abs(ret.ptime-ret.gen_perm_time-ret.apply_perm_time)<0.1,
              "Invalid phase timing");
      // Full authentication, identity and per-sample uniqueness verification
      // on each method's first measured output. Regression tests cover shapes.
      if (round==warmup) Verify(output,n,m,k,width,enclave.output_key);
      if (round<warmup) continue;
      std::printf("%s,%zu,%zu,%zu,%zu,%s,%zu,%d,%d,%.6f,%.6f,%.6f,%.6f,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
        name,n,m,k,width,method?"OFRSupple":"Supple",round-warmup,
        int(memory),int(profile),ret.gen_perm_time,ret.apply_perm_time,
        ret.ptime,elapsed,ret.algorithm_heap_peak_bytes,ret.offline_mark_ms,
        ret.offline_count_ms,ret.offline_swo_write_ms,ret.offline_tags_ms,
        ret.offline_normalize_ms,ret.offline_ofr_write_ms,ret.offline_replay_ms,
        ret.offline_project_ms,ret.offline_prepare_ms);
      std::fflush(stdout);
    }
  }
}
} // namespace
int main(int argc,char **argv) {
  try {
    Require(argc>=2,"Usage: paired_sgx_benchmark ENCLAVE [--rounds N] [--warmup N] [--matrix | --case N M K WIDTH] [--memory] [--profile]");
    size_t rounds=7,warmup=2,n=0,m=0,k=0,width=0;
    bool matrix=false,memory=false,profile=false;
    for (int arg=2;arg<argc;++arg) {
      const std::string token(argv[arg]);
      if (token=="--matrix") matrix=true;
      else if (token=="--memory") memory=true;
      else if (token=="--profile") profile=true;
      else if (token=="--rounds" && arg+1<argc) rounds=std::stoull(argv[++arg]);
      else if (token=="--warmup" && arg+1<argc) warmup=std::stoull(argv[++arg]);
      else if (token=="--case" && arg+4<argc) {
        n=std::stoull(argv[++arg]);m=std::stoull(argv[++arg]);
        k=std::stoull(argv[++arg]);width=std::stoull(argv[++arg]);
      } else throw std::invalid_argument("Unknown benchmark argument");
    }
    Require(rounds>0 && warmup<=SIZE_MAX-rounds,"Invalid round count");
    std::puts("case,n,m,k,width,method,round,memory_tracking,profile,offline_ms,online_ms,total_ms,ecall_ms,algorithm_heap_peak_bytes,mark_ms,count_ms,swo_write_ms,tags_ms,normalize_ms,write_ms,replay_ms,project_ms,prepare_ms");
    if (n) Run(argv[1],"custom",n,m,k,width,rounds,warmup,memory,profile);
    else if (matrix) {
      for (size_t size : {size_t(1)<<16,size_t(1)<<18,size_t(1)<<20,
                          size_t(1)<<22,size_t(1)<<24})
        Run(argv[1],"equal_k16",size,size/16,16,8,rounds,warmup,memory,profile);
      for (size_t size : {size_t(1)<<20,size_t(1)<<22})
        Run(argv[1],"equal_k64",size,size/64,64,16,rounds,warmup,memory,profile);
    } else {
      Run(argv[1],"compact",4096,16,16,16,rounds,warmup,memory,profile);
      Run(argv[1],"equal",4096,64,64,16,rounds,warmup,memory,profile);
      Run(argv[1],"frontier",4096,64,128,16,rounds,warmup,memory,profile);
      Run(argv[1],"mixed",4096,48,128,16,rounds,warmup,memory,profile);
      Run(argv[1],"equal_wide",4096,64,64,64,rounds,warmup,memory,profile);
      Run(argv[1],"equal_256",4096,64,64,256,rounds,warmup,memory,profile);
      Run(argv[1],"mixed_256",4096,48,128,256,rounds,warmup,memory,profile);
    }
  } catch (const std::exception &error) {
    std::fprintf(stderr,"%s\n",error.what());return 1;
  }
}
