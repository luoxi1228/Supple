This addendum covers the retained leaf8/direct-fields candidate. The prior
core bounds/public-trace review is in
`../../stage_04_workspace/diagnostics/vector_writer/audit.md`.

The fixed N=4 postorder sequence, with zero-based record coordinates, is
(0,1), (2,3), (0,2), (1,3). It consumes one packed four-gate group in exactly
that order. The fixed N=8 sequence concatenates this N=4 sequence on records
0..3, then the same sequence on records 4..7, then (0,4), (1,5), (2,6),
(3,7). It consumes three packed groups, exactly twelve original gates.
The N=2 case performs its original single (0,1) gate. Every larger network
continues the original left-child, right-child, parent-layer recursion.
These base cases therefore remove function calls and cursor operations
without removing, rearranging, or combining dependent network gates.

The choice between these bases depends only on public N. All record offsets
are public constants multiplied by the already validated record width.
Packed cursor groups still handle arbitrary public starting gate alignment.
Each decoded control uses the original OFork specialization and the original
two bits. The existing assembly count/behavior is preserved.

For valid tags x,y in 0..3, direct pair fields are:

```text
fixed = x & y
different = x ^ y
diagonal = ((different >> 1) & different) & ((x >> 1) ^ x) & 1
variable = different & (diagonal - 1)
left_type = (variable >> 1) & 1
right_type = variable & 1
```

The former temporary feature was
`fixed | (variable << 2) | (diagonal << 4)`. Its low two bits are exactly
fixed, bits two and three are the two variable flags, and bit four is
diagonal. Direct use of those fields is therefore identical to packing and
extracting them. The byte-vector implementation uses the same per-byte
masked shifts, and keeps arithmetic and selection in SIMD masks.
First-pass statistics, quotas, rank increments, responsibility tags,
canonical control encoding and direct packed postorder positions are
unchanged. The generic DFS and level-order layouts retain their interfaces.

Complete candidate OFR ASan/UBSan and production assembly regressions passed
before production was updated. The paired short benchmark checked its
outputs at 1M and 4M. No target HW validation has occurred. The source proof
retains the prior valid-input/caller-capacity assumptions and does not assert
a formal microarchitectural constant-time proof. New helper inlining can
change compiler register/stack usage; the previous stage04 stack report
must not be treated as an exact stack report for this new source.
