/**
 * The graphics card (step 97): which DXGI adapter the bridge's D3D11 device
 * is made on, and the display's.
 *
 * At the bridge's start (twoc_gpu_start, before xbox_d3d8's CreateDevice)
 * every adapter is listed in the log ([GPU]: name, its own memory, LUID,
 * Windows' high-performance and power-saving order where DXGI 1.6 has it).
 * Which one is used:
 *
 *   gpu = auto (also unset)   D3D11's default adapter, as before. Windows
 *                             picks it: its Graphics setting for this exe
 *                             (Settings > System > Display > Graphics), else
 *                             the driver's own choice, which the exe asks to
 *                             be the high-performance card by exporting
 *                             NvOptimusEnablement and
 *                             AmdPowerXpressRequestHighPerformance (main.c).
 *   gpu = <adapter name>      that card, matched by its name as DXGI gives it
 *                             (not by its place in the list, which changes),
 *                             through xbox_d3d8's d3d8_SetPreferredAdapter
 *                             (a local accessor, notes 18.100). A name this
 *                             PC doesn't have falls back to Automatic, with
 *                             a log line and the menu's amber note.
 *
 * RECOMP_GPU is the variable (crash_twoc.ini's gpu key fills it). It is read
 * once, at the start: a change in the menu applies at the next start.
 *
 * After the device is made (twoc_gpu_device_made) the log says which card
 * it is on and which card drives the window's display. On a laptop with
 * hybrid graphics they can differ (the NVIDIA GPU draws, the Intel GPU
 * drives the screen): the flip-model swap chain still presents there, with
 * Windows copying each frame across.
 */
#ifndef TWOC_GPU_H
#define TWOC_GPU_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TWOC_GPU_MAX 8

typedef struct {
    char name[128];                     /* DXGI's Description, UTF-8 */
    unsigned long long vram;            /* its own memory, bytes */
    unsigned long long shared;          /* system memory it may use */
    LUID luid;
    unsigned vendor, device;
    int high_perf_rank, min_power_rank; /* places in Windows' two orders (DXGI 1.6), -1 not known */
} twoc_gpu_t;

/* The bridge's start, before CreateDevice: list, log, and choose (RECOMP_GPU). */
void twoc_gpu_start(void);
/* After CreateDevice: `dev` the D3D11 device, `hwnd` the game window. Logs the card used and the display's. */
void twoc_gpu_device_made(void *dev, HWND hwnd);

/* The hardware adapters found at the start (software ones left out), in DXGI's order. */
int twoc_gpu_count(void);
const twoc_gpu_t *twoc_gpu_at(int i);
/* This run's choice as it was asked for at the start: "" Automatic, else the name. */
const char *twoc_gpu_asked(void);
/* "" or, when the name asked for isn't on this PC, the menu's note about it. */
const char *twoc_gpu_note(void);
/* The name of the card the device was made on ("" before it was). */
const char *twoc_gpu_in_use(void);
/* The card the device is on now and the window's display's card, as About's lines. */
void twoc_gpu_about(void *dev, HWND hwnd, char *used, size_t used_cap, char *display, size_t display_cap);
/* The adapter whose name is `name` (case and outer spaces aside), or -1. */
int twoc_gpu_find(const char *name);
/* "NVIDIA GeForce RTX 3050 Laptop GPU, 4.0 GB" */
void twoc_gpu_label(const twoc_gpu_t *g, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
