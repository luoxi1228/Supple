#include "FFOS_C_Opt.hpp"
#include "../../MemoryProfile.hpp"
#ifndef BEFTS_MODE
#include "../../ObliviousPrimitives.hpp"
#include "../../utils.hpp"
#endif
#include "../OnlineProfile.hpp"
#include "../OfflineProfile.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace ffos_c_opt_detail {
using namespace ffos_c_detail;
const size_t absent = std::numeric_limits<size_t>::max();
using Node = FFOS_C_OptControls::Node;

size_t Product(size_t a, size_t b) {
  size_t result = 0;
  if (MulOverflowSizeT(a, b, &result))
    throw std::length_error("FFOS_C_Opt size overflow");
  return result;
}
void Dimensions(size_t n, size_t m, size_t k) {
  if (!n || !m || !k || m > n)
    throw std::invalid_argument("Invalid FFOS_C_Opt dimensions");
  Product(m, k);
}
std::vector<FrontierNode> Roots(const std::vector<FrontierNode> &frontier,
                              size_t n, size_t m, size_t k) {
  auto roots = FfosCRootNodes(frontier, k);
  size_t next = 0;
  for (const auto &root : roots) {
    if (!root.count || root.start != next || Product(m, root.count) > n ||
        AddOverflowSizeT(next, root.count, &next))
      throw std::invalid_argument("Invalid FFOS_C_Opt frontier");
  }
  if (next != k) throw std::invalid_argument("Incomplete FFOS_C_Opt frontier");
  return roots;
}
size_t AddNode(FFOS_C_OptControls &controls, size_t n, size_t start,
               size_t count, size_t &position) {
  const size_t index = controls.nodes.size();
  controls.nodes.push_back({n, start, count, position, absent, absent});
  const size_t child_n = Product(controls.m, count);
  if (child_n < n && AddOverflowSizeT(position, n, &position))
    throw std::length_error("FFOS_C_Opt control overflow");
  if (count > 1) {
    const size_t left_k = count / 2;
    const size_t left = AddNode(controls, child_n, 0, left_k, position);
    const size_t right = AddNode(controls, child_n, left_k, count-left_k, position);
    controls.nodes[index].left = left;
    controls.nodes[index].right = right;
  }
  return index;
}

// Match FR's explicit capacity reservation and per-depth sibling projection
// buffers. An owned membership is consumed in place; public const roots copy.
struct MarkLevel {
  FfosCMarkWorkspace left, right, child;
};
struct MarkContext {
  explicit MarkContext(size_t k) : levels(WorkspaceDepth(k)+1) {}
  std::vector<MarkLevel> levels;
  std::vector<size_t> backup, root_copy;
  SelectedScratch selected;
};
void Reserve(FfosCMarkWorkspace &workspace, size_t items, size_t words) {
  workspace.mark_buf.reserve(Product(items, words));
  workspace.ensure_capacity(items, words);
}

// Projection commutes with the flag-driven compaction, exactly as in C.
// Forward projection also permits dst==src when the row width decreases.
void ProjectCompact(const size_t *src, size_t words, size_t *dst,
    const Node &node, FFOS_C_OptControls &controls, SelectedScratch &scratch) {
  const size_t n = node.input_blocks;
  const size_t dst_words = MarkWords(node.sample_count);
  const bool compact = Product(controls.m, node.sample_count) < n;
  bool *selected = compact ? scratch.acquire(n) : nullptr;
  if (compact && !selected) throw std::bad_alloc();
  size_t byte = node.control_offset/8, bit = node.control_offset%8;
  for (size_t i=0; i<n; ++i) {
    const size_t *row = src+i*words;
    size_t *out = dst+i*dst_words;
    if (dst_words == 1)
      out[0] = ProjectMarkSliceOneWord(row, words, node.sample_start,
                                      node.sample_count);
    else
      ProjectMarkSlice(row, words, node.sample_start, node.sample_count,
                       out, dst_words);
    if (compact) {
      size_t any = 0;
      for (size_t w=0; w<dst_words; ++w) any |= out[w];
      selected[i] = any != 0;
      const uint8_t mask = static_cast<uint8_t>(1U<<bit);
      const uint8_t value = static_cast<uint8_t>(0U-uint8_t(selected[i]));
      controls.bits[byte] = static_cast<uint8_t>(
          (controls.bits[byte]&~mask) | (value&mask));
      if (++bit == 8) { bit=0; ++byte; }
    }
  }
  if (compact)
    TightCompact_v2(reinterpret_cast<unsigned char *>(dst), n,
                    Product(dst_words, sizeof(size_t)), selected);
}

void WritePair(size_t *membership, size_t words, size_t left_id, size_t right_id,
    FFOS_C_OptControls &controls, MarkContext &context, size_t depth,
    enc_ret *profile) {
  const Node &left_node = controls.nodes[left_id];
  const Node &right_node = controls.nodes[right_id];
  const size_t n = left_node.input_blocks;
  online_profile::Track(profile ? &profile->offline_replay_ms : nullptr, [&] {
    std::copy(membership, membership+Product(n, words), context.backup.data());
  });
  MarkLevel &level = context.levels[depth];
  const size_t ids[] = {left_id, right_id};
  FfosCMarkWorkspace *destinations[] = {&level.left, &level.right};
  size_t *sources[] = {membership, context.backup.data()};
  for (size_t side=0; side<2; ++side) {
    const Node &node = controls.nodes[ids[side]];
    online_profile::Track(profile ? &profile->offline_swo_write_ms : nullptr, [&] {
      ProjectCompact(sources[side], words, sources[side], node, controls,
                     context.selected);
    });
    // FR also avoids projecting membership for exact-fit fork leaves.
    if (node.sample_count > 1)
      online_profile::Track(profile ? &profile->offline_project_ms : nullptr, [&] {
        const size_t items = Product(controls.m, node.sample_count);
        const size_t child_words = MarkWords(node.sample_count);
        Reserve(*destinations[side], items, child_words);
        std::copy(sources[side], sources[side]+Product(items,child_words),
                  destinations[side]->mark_ptr());
      });
  }
  if (left_node.sample_count > 1)
    WritePair(level.left.mark_ptr(), MarkWords(left_node.sample_count),
        left_node.left, left_node.right, controls, context, depth+1, profile);
  if (right_node.sample_count > 1)
    WritePair(level.right.mark_ptr(), MarkWords(right_node.sample_count),
        right_node.left, right_node.right, controls, context, depth+1, profile);
}

FFOS_C_OptControls Build(const std::vector<size_t> &membership,
    const std::vector<FrontierNode> &frontier, size_t n, size_t m, size_t k,
    enc_ret *profile, size_t *owned_root) {
  Dimensions(n,m,k);
  const size_t words = MarkWords(k);
  if (membership.size() != Product(n,words))
    throw std::invalid_argument("FFOS_C_Opt membership shape mismatch");
  FFOS_C_OptControls controls;
  controls.n=n; controls.m=m; controls.k=k;
  std::vector<FrontierNode> roots;
  online_profile::Track(profile ? &profile->offline_count_ms : nullptr, [&] {
    roots = Roots(frontier,n,m,k);
    controls.bit_count = FFOS_CControlCount(roots,n,m);
  });
  online_profile::Track(profile ? &profile->offline_prepare_ms : nullptr, [&] {
    const size_t twice_k = Product(k,2);
    controls.nodes.reserve(twice_k-roots.size());
    controls.roots.reserve(roots.size());
    size_t position = 0;
    for (const auto &root:roots)
      controls.roots.push_back(AddNode(controls,n,root.start,root.count,position));
    if (position != controls.bit_count)
      throw std::logic_error("FFOS_C_Opt plan/count mismatch");
  });
  controls.bits.resize(controls.bit_count/8+(controls.bit_count%8!=0),0);
  MarkContext context(k);
  const bool exact_root = frontier.empty() && Product(m,k)==n && k>1;
  size_t backup_words = exact_root ? Product(n,words) : 0;
  for (const auto &node:controls.nodes)
    if (node.sample_count>1)
      backup_words=std::max(backup_words,
          Product(Product(m,node.sample_count),MarkWords(node.sample_count)));
  context.backup.reserve(backup_words);
  context.backup.resize(backup_words);
  if (exact_root) {
    size_t *root = owned_root;
    if (!root) {
      online_profile::Track(profile ? &profile->offline_replay_ms : nullptr, [&] {
        context.root_copy.reserve(membership.size());
        context.root_copy.assign(membership.begin(),membership.end());
      });
      root=context.root_copy.data();
    }
    WritePair(root,words,controls.roots[0],controls.roots[1],controls,context,0,profile);
  } else {
    FfosCMarkWorkspace &child=context.levels[0].child;
    for (size_t id:controls.roots) {
      const Node &node=controls.nodes[id];
      if (node.sample_count==1 && Product(m,node.sample_count)==n) continue;
      online_profile::Track(profile ? &profile->offline_swo_write_ms : nullptr, [&] {
        Reserve(child,n,MarkWords(node.sample_count));
        ProjectCompact(membership.data(),words,child.mark_ptr(),node,controls,
                       context.selected);
      });
      if (node.sample_count>1)
        WritePair(child.mark_ptr(),MarkWords(node.sample_count),node.left,node.right,
                  controls,context,1,profile);
    }
  }
  return controls;
}

void ValidateNode(const FFOS_C_OptControls &controls, size_t id, size_t n,
    size_t start, size_t count, size_t &next, size_t &position) {
  if (id!=next || next>=controls.nodes.size())
    throw std::length_error("FFOS_C_Opt missing/noncanonical node");
  const Node &node=controls.nodes[next++];
  if (node.input_blocks!=n || node.sample_start!=start ||
      node.sample_count!=count || node.control_offset!=position)
    throw std::logic_error("FFOS_C_Opt node shape/offset mismatch");
  const size_t out=Product(controls.m,count);
  if (out>n) throw std::length_error("FFOS_C_Opt node exceeds input");
  if (out<n && AddOverflowSizeT(position,n,&position))
    throw std::length_error("FFOS_C_Opt control overflow");
  if (count>1) {
    const size_t left=count/2;
    ValidateNode(controls,node.left,out,0,left,next,position);
    ValidateNode(controls,node.right,out,left,count-left,next,position);
  } else if (node.left!=absent || node.right!=absent)
    throw std::logic_error("FFOS_C_Opt leaf has children");
}
void Validate(const FFOS_C_OptControls &controls,size_t n,size_t m,size_t k) {
  Dimensions(n,m,k);
  if (controls.n!=n || controls.m!=m || controls.k!=k || controls.roots.empty())
    throw std::invalid_argument("FFOS_C_Opt plan dimensions mismatch");
  size_t next=0, position=0, samples=0;
  for (size_t id:controls.roots) {
    if (id>=controls.nodes.size()) throw std::length_error("FFOS_C_Opt root missing");
    const size_t count=controls.nodes[id].sample_count;
    if (!count || count>k-samples)
      throw std::length_error("FFOS_C_Opt root coverage mismatch");
    ValidateNode(controls,id,n,samples,count,next,position);
    samples+=count;
  }
  if (samples!=k || next!=controls.nodes.size() || position!=controls.bit_count ||
      controls.bits.size()!=position/8+(position%8!=0))
    throw std::length_error("FFOS_C_Opt plan/tape length mismatch");
}
void CompactData(unsigned char *data,const Node &node,
    const FFOS_C_OptControls &controls,size_t width,SelectedScratch &scratch,
    enc_ret *profile) {
  if (Product(controls.m,node.sample_count)==node.input_blocks) return;
  bool *selected=scratch.acquire(node.input_blocks);
  if (!selected) throw std::bad_alloc();
  online_profile::Track(profile ? &profile->online_route_ms : nullptr,[&] {
    UnpackControlBitsToBoolArray(controls.bits,node.control_offset,node.input_blocks,selected);
    TightCompact_v2(data,node.input_blocks,width,selected);
  });
}
void ReadPair(unsigned char *data,size_t left_id,size_t right_id,
    const FFOS_C_OptControls &controls,size_t width,unsigned char *backup,
    SelectedScratch &selected,enc_ret *profile) {
  const Node &left=controls.nodes[left_id], &right=controls.nodes[right_id];
  online_profile::Track(profile ? &profile->online_copy_ms : nullptr,[&] {
    std::memcpy(backup,data,Product(left.input_blocks,width));
  });
  CompactData(data,left,controls,width,selected,profile);
  CompactData(backup,right,controls,width,selected,profile);
  const size_t left_bytes=Product(Product(controls.m,left.sample_count),width);
  online_profile::Track(profile ? &profile->online_copy_ms : nullptr,[&] {
    std::memcpy(data+left_bytes,backup,Product(Product(controls.m,right.sample_count),width));
  });
  // Both child inputs are now in their final disjoint output slices. The
  // parent backup is dead before recursion, so every depth shares it.
  if (left.sample_count>1)
    ReadPair(data,left.left,left.right,controls,width,backup,selected,profile);
  if (right.sample_count>1)
    ReadPair(data+left_bytes,right.left,right.right,controls,width,backup,selected,profile);
}
} // namespace ffos_c_opt_detail

FFOS_C_OptControls FFOS_C_OptControl(const std::vector<size_t> &membership,
    const std::vector<FrontierNode> &frontier,size_t n,size_t m,size_t k,enc_ret *profile) {
  return ffos_c_opt_detail::Build(membership,frontier,n,m,k,profile,nullptr);
}
ControlReadResult FFOS_C_OptControlRead(const unsigned char *data,
    const FFOS_C_OptControls &controls,size_t n,size_t m,size_t k,size_t width,
    unsigned char *samples,size_t capacity,enc_ret *profile) {
  using namespace ffos_c_opt_detail;
  Validate(controls,n,m,k);
  const size_t blocks=Product(m,k), input_bytes=Product(n,width);
  const size_t output_bytes=Product(blocks,width);
  Product(capacity,width);
  if (!data || !samples || !width || capacity<blocks)
    throw std::invalid_argument("Invalid FFOS_C_Opt buffers");
  const uintptr_t a=reinterpret_cast<uintptr_t>(data), b=reinterpret_cast<uintptr_t>(samples);
  if (a<=b ? b-a<input_bytes : a-b<output_bytes)
    throw std::invalid_argument("FFOS_C_Opt input/output overlap");
  std::vector<unsigned char> backup;
  if (controls.bit_count) { backup.reserve(input_bytes); backup.resize(input_bytes); }
  SelectedScratch selected;
  const bool exact_root=controls.roots.size()==2 && Product(m,k)==n &&
      controls.nodes[controls.roots[0]].sample_count==k/2;
  if (exact_root) {
    online_profile::Track(profile ? &profile->online_copy_ms : nullptr,[&] {
      std::memcpy(samples,data,input_bytes);
    });
    ReadPair(samples,controls.roots[0],controls.roots[1],controls,width,
             backup.data(),selected,profile);
  } else {
    size_t written=0;
    for (size_t id:controls.roots) {
      const Node &node=controls.nodes[id];
      unsigned char *out=samples+Product(written,width);
      const size_t child_blocks=Product(m,node.sample_count);
      if (node.sample_count==1 && child_blocks==n)
        online_profile::Track(profile ? &profile->online_copy_ms : nullptr,[&] {
          std::memcpy(out,data,input_bytes);
        });
      else {
        online_profile::Track(profile ? &profile->online_copy_ms : nullptr,[&] {
          std::memcpy(backup.data(),data,input_bytes);
        });
        CompactData(backup.data(),node,controls,width,selected,profile);
        online_profile::Track(profile ? &profile->online_copy_ms : nullptr,[&] {
          std::memcpy(out,backup.data(),Product(child_blocks,width));
        });
        if (node.sample_count>1)
          ReadPair(out,node.left,node.right,controls,width,backup.data(),selected,profile);
      }
      written+=child_blocks;
    }
  }
  return {blocks,controls.bit_count};
}
std::vector<unsigned char> FFOS_C_OptApply(const unsigned char *data,
    const FFOS_C_OptControls &controls,size_t n,size_t m,size_t k,size_t width) {
  using namespace ffos_c_opt_detail;
  Validate(controls,n,m,k);
  std::vector<unsigned char> output(Product(Product(m,k),width));
  FFOS_C_OptControlRead(data,controls,n,m,k,width,output.data(),Product(m,k));
  return output;
}
std::vector<unsigned char> FFOS_C_Opt(const unsigned char *data,
    size_t n,size_t m,size_t k,size_t width) {
  using namespace ffos_c_opt_detail;
  if (!data || !n || !m || !k || !width) return {};
  m=std::min(m,n);
  const auto frontier=Product(m,k)>n ? FFOS_CFrontier(n,m,k) : std::vector<FrontierNode>{};
  auto membership=FFOS_CMark(n,m,k);
  const auto controls=Build(membership,frontier,n,m,k,nullptr,membership.data());
  std::vector<size_t>().swap(membership);
  return FFOS_C_OptApply(data,controls,n,m,k,width);
}
extern "C" void DecFFOS_C_Opt(unsigned char *encrypted_buffer,
    size_t N,size_t M,size_t K,size_t encrypted_width,
    unsigned char *encrypted_result,enc_ret *ret) {
  using namespace ffos_c_opt_detail;
  if (!ret) return;
  memory_profile::Scope memory(ret);
  ret->ptime=ret->gen_perm_time=ret->apply_perm_time=0.0;
  ret->online_route_ms=ret->online_reorder_ms=0.0;
  ret->online_copy_ms=ret->online_shuffle_ms=0.0;
  offline_profile::Reset(ret);
#ifdef COUNT_OSWAPS
  ret->OSWAP_count=0;
#endif
  if (!encrypted_buffer || !N || !M || !K || !encrypted_result) return;
  unsigned char *decrypted=nullptr;
  const size_t width=decryptBuffer(encrypted_buffer,N,encrypted_width,&decrypted);
  if (!decrypted || !width || width==SIZE_MAX) { free(decrypted); return; }
  M=std::min(M,N);
  bool prb_started=false;
  try {
    const size_t blocks=Product(M,K), bytes=Product(blocks,width);
    memory.BeginOffline();
    PRB_pool_init(1); prb_started=true;
    long start=0,end=0;
    ocall_clock(&start);
    std::vector<size_t> membership;
    online_profile::Track(ret->collect_offline_profile ? &ret->offline_mark_ms : nullptr,[&] {
      membership=FFOS_CMark(N,M,K);
    });
    std::vector<FrontierNode> frontier;
    online_profile::Track(ret->collect_offline_profile ? &ret->offline_count_ms : nullptr,[&] {
      if (blocks>N) frontier=FFOS_CFrontier(N,M,K);
    });
    const auto controls=Build(membership,frontier,N,M,K,
        ret->collect_offline_profile ? ret : nullptr,membership.data());
    std::vector<size_t>().swap(membership);
    ocall_clock(&end);
    ret->gen_perm_time=static_cast<double>(end-start)/1000.0;
    memory.EndOffline();
    if (ret->collect_offline_profile) ret->offline_heap_peak_bytes=offline_profile::HeapPeakBytes();
#ifdef COUNT_OSWAPS
    const uint64_t initial=OSWAP_COUNTER;
#endif
    memory.BeginOnline();
    std::vector<unsigned char> plain(bytes);
    ocall_clock(&start);
    FFOS_C_OptControlRead(decrypted,controls,N,M,K,width,plain.data(),blocks,
                         ret->collect_online_profile ? ret : nullptr);
    ocall_clock(&end);
    ret->apply_perm_time=static_cast<double>(end-start)/1000.0;
    memory.EndOnline();
    ret->ptime=ret->gen_perm_time+ret->apply_perm_time;
    if (ret->collect_offline_profile) ret->total_heap_peak_bytes=offline_profile::HeapPeakBytes();
#ifdef COUNT_OSWAPS
    ret->OSWAP_count=OSWAP_COUNTER-initial;
#endif
    if (encryptBuffer(plain.data(),blocks,width,encrypted_result)!=SIZE_MAX) memory.Complete();
  } catch (const std::exception &) {
    ret->ptime=ret->gen_perm_time=ret->apply_perm_time=0.0;
    ret->online_route_ms=ret->online_reorder_ms=0.0;
    ret->online_copy_ms=ret->online_shuffle_ms=0.0;
    offline_profile::Reset(ret);
  }
  if (prb_started) PRB_pool_shutdown();
  free(decrypted);
}
