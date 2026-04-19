// Unit tests for multi-start thread-start synchronization algorithm.
//
// Tests the spindle_arm_start_offset() countdown model:
//   - Offset computation: k * PPR/N for N=1,2,3,4,6,8, k=0..N-1
//   - State machine: arm → index → (optional edge countdown) → latch
//   - PPR divisibility guard (PPR % N == 0 required before arming)
//
// The spindle.c ring-buffer drain is hardware-coupled (DMA write_addr),
// so this file re-implements the countdown state machine as a reference
// model and drives it with synthetic spindle events.  Hardware integration
// is validated by bring-up Steps 1 and 2.
//
// Compile: gcc -Wall -std=c11 -o test_multistart test_multistart.c -lm

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// ---- Reference model of spindle multi-start countdown state machine ----
// Mirrors spindle.c g_waiting_index / g_waiting_offset / g_offset_remain.

typedef struct {
    bool     waiting_index;
    bool     waiting_offset;
    uint32_t offset_remain;
    bool     index_latched;
} multistart_t;

static void ms_arm(multistart_t *m, uint32_t pulses) {
    m->waiting_index  = true;
    m->waiting_offset = false;
    m->offset_remain  = pulses;
    m->index_latched  = false;
}

// Returns true if latch just fired.
static bool ms_edge(multistart_t *m) {
    if (m->waiting_offset && m->offset_remain > 0) {
        m->offset_remain--;
        if (m->offset_remain == 0) {
            m->waiting_offset = false;
            m->index_latched  = true;
            return true;
        }
    }
    return false;
}

// Call on rising edge of index signal.
static void ms_index(multistart_t *m) {
    if (!m->waiting_index) return;
    m->waiting_index = false;
    if (m->offset_remain == 0) {
        m->index_latched = true;
    } else {
        m->waiting_offset = true;
    }
}

// ---- Helpers ----

// Compute start offset for start k of N at PPR counts per revolution.
// Caller must ensure PPR % N == 0.
static uint32_t offset_for_start(uint32_t ppr, uint32_t N, uint32_t k) {
    return k * (ppr / N);
}

// Drive the model: fire index, then drive `offset` quadrature edges.
// Returns the total edges needed before latch fires (0 means index fired it).
static uint32_t simulate_arm(uint32_t ppr, uint32_t N, uint32_t k) {
    multistart_t m = {0};
    uint32_t offset = offset_for_start(ppr, N, k);
    ms_arm(&m, offset);

    // Fire the index pulse
    ms_index(&m);

    if (m.index_latched) return 0;   // single-start or k==0

    // Drive edge countdown
    uint32_t edges = 0;
    while (!m.index_latched && edges < ppr + 1) {
        ms_edge(&m);
        edges++;
    }
    assert(m.index_latched);         // must have latched
    return edges;
}

#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

// ---- Tests ----

// k=0 always fires on index directly (offset==0), any N
TEST(test_k0_always_immediate) {
    uint32_t ppr = 4000;
    uint32_t Ns[] = {1, 2, 3, 4, 6, 8};
    for (size_t i = 0; i < sizeof(Ns)/sizeof(Ns[0]); i++) {
        uint32_t N = Ns[i];
        // PPR must be divisible by N
        if (ppr % N != 0) continue;
        uint32_t edges = simulate_arm(ppr, N, 0);
        assert(edges == 0);
    }
}

// N=1 single-start: only k=0 exists; fires on index
TEST(test_single_start_fires_on_index) {
    multistart_t m = {0};
    ms_arm(&m, 0);
    assert(!m.index_latched);
    ms_index(&m);
    assert(m.index_latched);
}

// N=2: k=0 → 0 edges, k=1 → PPR/2 edges
TEST(test_two_start_offsets) {
    uint32_t ppr = 4000;
    assert(simulate_arm(ppr, 2, 0) == 0);
    assert(simulate_arm(ppr, 2, 1) == ppr / 2);
}

// N=3: offsets are 0, PPR/3, 2*PPR/3
TEST(test_three_start_offsets) {
    uint32_t ppr = 4000;  // 4000 % 3 != 0, use 3000
    ppr = 3000;
    assert(simulate_arm(ppr, 3, 0) == 0);
    assert(simulate_arm(ppr, 3, 1) == 1000);
    assert(simulate_arm(ppr, 3, 2) == 2000);
}

// N=4: offsets at 0, PPR/4, PPR/2, 3*PPR/4
TEST(test_four_start_offsets) {
    uint32_t ppr = 4000;
    for (uint32_t k = 0; k < 4; k++) {
        uint32_t edges = simulate_arm(ppr, 4, k);
        assert(edges == k * (ppr / 4));
    }
}

// N=6: all six offsets
TEST(test_six_start_offsets) {
    uint32_t ppr = 6000;
    for (uint32_t k = 0; k < 6; k++) {
        uint32_t edges = simulate_arm(ppr, 6, k);
        assert(edges == k * (ppr / 6));
    }
}

// N=8: all eight offsets
TEST(test_eight_start_offsets) {
    uint32_t ppr = 8000;
    for (uint32_t k = 0; k < 8; k++) {
        uint32_t edges = simulate_arm(ppr, 8, k);
        assert(edges == k * (ppr / 8));
    }
}

// Latch is NOT set before index arrives
TEST(test_no_latch_before_index) {
    multistart_t m = {0};
    ms_arm(&m, 500);
    // Drive edges without firing index — should never latch
    for (int i = 0; i < 600; i++) ms_edge(&m);
    assert(!m.index_latched);
}

// Arm again resets the countdown
TEST(test_rearm_resets_state) {
    multistart_t m = {0};
    ms_arm(&m, 100);
    ms_index(&m);
    assert(m.waiting_offset);

    // Re-arm before latch fires
    ms_arm(&m, 0);
    ms_index(&m);
    assert(m.index_latched);
}

// PPR divisibility guard: PPR % N != 0 must be rejected before calling arm
TEST(test_ppr_divisibility) {
    // Verify the formula is exact only when PPR is divisible
    assert(4000 % 2 == 0);
    assert(4000 % 4 == 0);
    assert(4000 % 8 == 0);
    assert(3000 % 3 == 0);
    assert(6000 % 6 == 0);
    // Non-divisible cases would produce fractional offsets — must be blocked at arm time
    assert(4000 % 3 != 0);  // would lose 1 count per start
    assert(1000 % 6 != 0);  // would lose counts
}

// Boundary: exactly offset_remain edges fires latch (not one too many or few)
TEST(test_countdown_boundary_exact) {
    uint32_t ppr = 4000;
    uint32_t offset = ppr / 4;  // 1000

    multistart_t m = {0};
    ms_arm(&m, offset);
    ms_index(&m);

    // Drive 999 edges — must NOT latch yet
    for (uint32_t i = 0; i < offset - 1; i++) ms_edge(&m);
    assert(!m.index_latched);

    // 1000th edge — must latch
    ms_edge(&m);
    assert(m.index_latched);
}

// After latch fires, further edges do not toggle it off
TEST(test_latch_stays_set) {
    multistart_t m = {0};
    ms_arm(&m, 0);
    ms_index(&m);
    assert(m.index_latched);

    // More edges — latch stays true (cleared separately by caller)
    for (int i = 0; i < 100; i++) ms_edge(&m);
    assert(m.index_latched);
}

int main(void) {
    printf("=== test_multistart ===\n");
    RUN(test_k0_always_immediate);
    RUN(test_single_start_fires_on_index);
    RUN(test_two_start_offsets);
    RUN(test_three_start_offsets);
    RUN(test_four_start_offsets);
    RUN(test_six_start_offsets);
    RUN(test_eight_start_offsets);
    RUN(test_no_latch_before_index);
    RUN(test_rearm_resets_state);
    RUN(test_ppr_divisibility);
    RUN(test_countdown_boundary_exact);
    RUN(test_latch_stays_set);
    printf("All passed.\n");
    return 0;
}
