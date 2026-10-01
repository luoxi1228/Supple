Static core audit, 2026-10-02. This review used source reads only. No compiler,
test or performance run was started during the review. The separately
authorized boundary fixes listed below are frozen and await the root agent's
regression window. Earlier regression and SDK-only compilation results do not
by themselves validate those subsequent fixes.

For a valid normalized balanced residue of public length L, the two tag-bit
weights are each L/2. If F is a side's fixed-pair count, D is the diagonal
count and V is its variable-pair count, then 2F + D + V = L/2. Consequently
F <= L/4, diagonal quota floor(D/2) <= L/4, and each side's quota is in
0..L/4. The left quota is ceil(V_left/2) and the right quota is
floor(V_right/2), with parity supplied by the same identity. The L=2 case
uses its direct control rule and does not compute these quotas.

The byte path is selected only for public L <= 256 and tiles of at least
16 residues. Its quota is at most 64. At every gate's signed comparison,
the rank is at most L/2 - 1 <= 127. Rank 128 can occur only after the last
gate, and is never compared again. Statistics and rank additions explicitly
use unsigned byte vectors. A diagonal statistic of 128 is divided with the
logical, masked byte shift; it is not used as a signed quota. Fixed-side
statistics stay at most 64, so signed quota subtraction stays in range.

The short path is selected only for public L <= 65536 and tiles of at least
eight residues. The analogous bounds are quota <= 16384 and compared rank
<= 32767. Rank/statistic 32768 is handled by unsigned short additions and
logical unsigned division, and terminal rank 32768 is never compared again.
The 32-bit path additionally requires public root N <= INT32_MAX. The
scalar size_t path remains the fallback. Width selection, fallback choice,
tile size and loop counts depend on public dimensions only.

The early prefix path requires public stride <= 4, L >= 8 and
N <= INT32_MAX. A power-of-two gate count L/2 is therefore divisible by four;
each batch loads and writes exactly four complete pairs, without a partial
tail. PrefixChoice4 selects a type's first quota occurrences using the
exclusive prefix count before each lane. Its remaining counter equals quota
minus all preceding occurrences, which is equivalent to the DFS rank rule.
Negative remaining counters are intentional after the quota has been used.
Their magnitude is bounded by L/2, so the signed 32-bit subtraction is safe.

At stride s, a tile is a power of two no larger than s, so it divides the
public residue range. Tag and membership row indices stay in 0..N-1.
The postorder start for the first gate is s*log2(s); advancing a gate uses
s*(depth + 1 + (2 << ctz(gate+1)) - 2). ctz is called only with a positive
public argument. The largest carry shift stays below the size_t width for
any accepted balanced control count. Starts are monotone positions inside
the already checked G=(N/2)*log2(N) tape, so their intermediate additions
cannot overflow when G fits. The stride-one two-gate stores combine two
consecutive starts; their advancement is the sum of those same two original
increments. View range checks and packed masks retain all neighboring gates,
including a node's nonaligned start and final shared byte.

The source-level public-trace argument covers valid inputs and the intended
call contracts: generated/normalized tags are in 0..3, supplied capacities
match them, the internal normalized entry points receive the cached correct
gate count, and caller-owned membership storage contains N*words size_t
elements. Gate types, quotas, ranks and controls feed arithmetic, SIMD
comparisons and masks. They do not index lookup tables or choose addresses.
Loop bounds and addresses use shape, stride, residue, tile, word width and
control offset. Scalar membership routing loads both inputs and writes both
outputs. SIMD routing does the same using masks. Cursor reload/alignment
branches depend on the public tape position. Assembly specializations remain
selected by public record width.

Invalid-tag/capacity/control checks may take an exception path depending on
the invalid value; the public-trace claim does not assert identical traces
for invalid inputs. Packed utility initialization arguments are public in
the generation paths. Unchecked cursor operations require a prevalidated
complete logical span, and a view must not outlive or be invalidated by its
owner. This is a source-level data-flow argument, not a formal proof about
every compiled binary or microarchitectural leakage mechanism.

The tile selector budgets 9 counter fields, two tag bytes, and two membership
rows per residue: tile*(9*counter_bytes + 2 + 2*words*sizeof(size_t)). It uses
the public counter width and takes a power of two at most 256. If that
budgeted maximum tile is below the chosen SIMD lane count, it recomputes
using the conservative size_t estimate. A small stride may instead clamp
the tile below the lane count and select a wider fallback counter; those
very small tiles leave budget slack, but the stated narrow-width expression
is not an exact byte count for such fallback execution.
The 24 KiB quantity is a logical active-tile estimate, not a strict bound on
all cache lines, control-stream traffic, compiler spills, or the stack frame.
Very wide rows have an explicit exception: a one-residue tile is retained
even when one pair of rows plus metadata exceeds the budget. For example,
1536 words per row on this 64-bit build already use 24576 bytes for the two
rows alone. The early prefix path batches four gates independently of this
residue tile selector and streams wider membership rows through the scalar
word loop; no universal 24 KiB physical-residency guarantee is claimed.

The earlier exact SDK-only -fstack-usage compile reported 30656 bytes for the
routing tiled function's own bounded frame and 30320 bytes for its no-route
variant. 30656 bytes is about 30.66 decimal kB or 29.94 KiB, rather than
30.7 KiB. The prefix callee reported another 384-byte frame. These are
individual compiler frame reports; they do not establish the complete call
chain's stack peak. Heap-peak tracking excludes those stack frames.

The review found and corrected these public-dimension boundaries in
Enclave/SubSample_v2/OFR/OFR.cpp:

- Lines 1499, 1604, 1617 and 1632 require sizeof(size_t)==8 before selecting
  the specialized one-word SIMD membership routes. Those routes operate on
  64-bit rows. Other architectures now use the scalar word loop; no 32-bit
  runtime validation was performed during this review.
- Lines 2102 and 2106 require block_size<=UINT32_MAX before the level-order
  variable-width assembly specializations. This matches the postorder path
  and prevents narrowing a larger public byte width to the assembly ABI.
  The missing guard was also present in the original HEAD implementation.
- Lines 1856, 1896 and 2202 check the membership byte product as well as its
  word product. If N*words fits size_t but N*words*sizeof(size_t) does not,
  generation/replay throws std::invalid_argument with
  "Invalid OFR membership dimensions" before writing tags, controls or
  membership rows. Actual allocation capacity still belongs to the caller,
  because the membership API accepts a pointer and public dimensions.

All new dispatch guards use public dimensions. On the existing 64-bit
benchmark widths, they retain the same SIMD/assembly path. The final
regression must include the newly added byte-product overflow fixtures and
the previously prepared 128/32768 terminal-rank cases before the stage is
accepted. The native short diagnostic still missed the end-to-end target;
this audit does not change that performance conclusion.
