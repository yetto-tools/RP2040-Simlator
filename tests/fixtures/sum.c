/* Integration fixture: a freestanding Cortex-M0+ program compiled by
 * arm-none-eabi-gcc and executed by the simulator. No libc, no startup.
 *
 * _start computes sum(1..10) = 55, verifies BSS is zero-initialised, writes
 * the result to a known global, and spins. The test loads this ELF, runs it,
 * and checks the global. */

volatile unsigned g_result;   /* .bss */
volatile unsigned g_bss_zero; /* .bss - must read back as 0 */

__attribute__((used)) unsigned sum_to(unsigned n) {
    unsigned s = 0;
    for (unsigned i = 1u; i <= n; ++i) s += i;
    return s;
}

__attribute__((noreturn)) void _start(void) {
    unsigned r = sum_to(10u);          /* 55 */
    if (g_bss_zero == 0u) r += 1000u;  /* 1055 if BSS is clean */
    g_result = r;
    for (;;) { __asm__ volatile("" ::: "memory"); }
}
