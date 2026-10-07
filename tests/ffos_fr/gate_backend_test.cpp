// OFR.cpp is linked separately without BEFTS_MODE, exercising real assembly
// and SSE2, unlike the generic host adapters used by the routing regression.
#include "../ffos_c/host_support.hpp"
#include "../../Enclave/SubSample_v2/FFOS_C/helper.cpp"
#include "../../Enclave/SubSample_v2/FFOS_C/FFOS_C.cpp"
#include "../../Enclave/SubSample_v2/FFOS_FR/FFOS_FR.cpp"
#include "../../Enclave/SubSample_v2/FFOS_FR_Opt/FFOS_FR_Opt.cpp"

thread_local uint64_t OSWAP_COUNTER = 0;
namespace ofr {
uint64_t test_scalar_gate_calls = 0;
uint64_t test_four_gate_batches = 0;
}

static void ResetGates() {
  OSWAP_COUNTER = 0;
  ofr::test_scalar_gate_calls = ofr::test_four_gate_batches = 0;
}

static void CheckGates(size_t expected, bool optimized, size_t n, size_t width) {
  assert(OSWAP_COUNTER == expected);
  assert(ofr::test_scalar_gate_calls + 4 * ofr::test_four_gate_batches == expected);
  if (!optimized || n < 8 || (width != 8 && width != 16))
    assert(ofr::test_four_gate_batches == 0);
  else
    assert(ofr::test_four_gate_batches > 0);
}

int main() {
  size_t checked = 0;
  for (size_t n : {2U, 4U, 8U, 16U, 32U, 64U, 128U})
    for (size_t width : {4U, 8U, 12U, 16U, 24U, 32U, 64U, 256U})
      for (size_t offset = 0; offset < 4; ++offset) {
        const size_t gates = ofr::OFRControlCount(n, n / 2, n / 2);
        ofr::PackedControls controls(offset + gates + 3);
        for (size_t i = 0; i < controls.size(); ++i)
          controls[i] = static_cast<uint8_t>(rng() & 3U);
        const size_t guard = 13;
        std::vector<unsigned char> original(n * width + 2 * guard);
        for (auto &byte : original) byte = static_cast<unsigned char>(rng());
        auto scalar = original, optimized = original;
        ResetGates();
        ofr::OFRApplyPreparedPostOrderInPlace(scalar.data() + guard,
            controls.view(offset, gates), n, n / 2, n / 2, width, false);
        CheckGates(gates, false, n, width);
        ResetGates();
        ofr::OFRApplyPreparedPostOrderInPlace(optimized.data() + guard,
            controls.view(offset, gates), n, n / 2, n / 2, width, true);
        CheckGates(gates, true, n, width);
        assert(scalar == optimized);
        assert(std::equal(original.begin(), original.begin() + guard, scalar.begin()));
        assert(std::equal(original.end() - guard, original.end(), scalar.end() - guard));
        ++checked;
      }

  // Public FR APIs and both ECALL wrappers must select their intended backend.
  for (size_t width : {8U, 16U, 24U}) {
    const size_t n = 128, m = 2, k = 64;
    std::vector<unsigned char> data(n * width);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<unsigned char>(i * 37);
    const auto input_before = data;
    const auto membership = FFOS_CMark(n, m, k);
    const auto controls = FFOS_FRControl(membership, {}, n, m, k);
    const size_t gates = controls.ofr.size();
    ResetGates();
    const auto scalar = FFOS_FRApply(data.data(), controls, {}, n, m, k, width);
    CheckGates(gates, false, n, width);
    ResetGates();
    const auto optimized = FFOS_FR_OptApply(data.data(), controls, {}, n, m, k, width);
    CheckGates(gates, true, n, width);
    assert(scalar == optimized);
    assert(data == input_before);
    std::vector<unsigned char> encrypted(n * width);
    enc_ret ret{};
    ResetGates();
    DecFFOS_FR(data.data(), n, m, k, width, encrypted.data(), &ret);
    // Offline work is allowed to count separately; these probes count online
    // StyledGate calls only. No optimized batch may occur in mode 5.
    assert(ofr::test_four_gate_batches == 0);
    assert(ofr::test_scalar_gate_calls == gates);
    assert(ret.OSWAP_count == gates);
    ResetGates();
    DecFFOS_FR_Opt(data.data(), n, m, k, width, encrypted.data(), &ret);
    assert(ofr::test_scalar_gate_calls + 4 * ofr::test_four_gate_batches == gates);
    assert((ofr::test_four_gate_batches != 0) == (width == 8 || width == 16));
    assert(ret.OSWAP_count == gates);
  }
  std::printf("PASS: %zu real OFork/SSE2 equivalence cases; scalar and Opt API/ECALL dispatch and gate counts\n", checked);
}
