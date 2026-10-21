/**
 * Memory logging (step 99): a [MEM] line in each 10-s interval.
 *
 * The power log's thread (power_log.c) takes one sample every 10 s and at
 * its start (mem_log_sample); nothing runs per frame. A sample holds:
 *
 *   - the process: private bytes (its commit charge) and their peak, the
 *     working set and its peak, the system's commit and its limit
 *     (K32GetProcessMemoryInfo, K32GetPerformanceInfo: kernel32, so the exe
 *     imports nothing new); handles, threads, GDI and USER objects;
 *   - the render adapter's GPU memory (IDXGIAdapter3::QueryVideoMemoryInfo,
 *     gpu.c): what the process uses and Windows' budget for it, local (the
 *     card's own) and non-local (system memory it maps);
 *   - the bridge's own holders (d3d_bridge.c, d3d_host.c: the vertex cache,
 *     the texture table and the recorded texture copies, the baked-draw
 *     memory and the rings, the frame slots' arenas, the render targets, the
 *     AO textures when on), read without a lock (sizes only, a log line);
 *   - the guest's heap, contiguous arena, pool and VM zone (recomp_manual.c,
 *     the figures the [HEAPSTAT] lines give).
 *
 * The bridge's interval block prints the latest sample as its [MEM] line
 * (mem_log_last); the power log's file (RECOMP_POWER_LOG) gets it every 10 s
 * too. sweep/memfit.py fits each quantity's slope over a run.
 */
#ifndef TWOC_MEM_LOG_H
#define TWOC_MEM_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Takes a sample (the power log's thread); `now_s` the bridge's clock, as its lines count it. */
void mem_log_sample(double now_s);
/* The latest sample's text, without "[MEM] " and the newline; "" before the first. Any thread. */
void mem_log_last(char *buf, size_t cap);

/* The holders' own texts, each a few "name N unit" items (no newline). */
void d3db_mem_text(char *buf, size_t cap);          /* d3d_bridge.c: the vertex cache */
void d3dh_mem_text(char *buf, size_t cap);          /* d3d_host.c: the host side's holders */
void twoc_guest_mem_text(char *buf, size_t cap);    /* recomp_manual.c: the guest's heap, arena, pool, zone */

#ifdef __cplusplus
}
#endif

#endif
