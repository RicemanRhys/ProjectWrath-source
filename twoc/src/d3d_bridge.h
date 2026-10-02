/**
 * D3D mirror bridge (Option B): the interface between the wrappers in
 * recomp_manual.c and d3d_bridge.c. See d3d_bridge.c.
 */
#ifndef TWOC_D3D_BRIDGE_H
#define TWOC_D3D_BRIDGE_H

#include <stdint.h>

/* Wrapped entry points. The first 15 are the bridge's (notes 18.31); the rest
 * are census-only probes for the step 29 identities marked "probable". */
enum {
    D3DB_CREATE_DEVICE,     /* 0x0FD6E0 Direct3D_CreateDevice */
    D3DB_CLEAR,             /* 0x100EA0 D3DDevice_Clear */
    D3DB_SWAP,              /* 0x100C40 D3DDevice_Swap */
    D3DB_DRAW_UP,           /* 0x1019C0 D3DDevice_DrawVerticesUP */
    D3DB_DRAW,              /* 0x101B20 D3DDevice_DrawVertices */
    D3DB_DRAW_INDEXED,      /* 0x101BC0 D3DDevice_DrawIndexedVertices */
    D3DB_BEGIN,             /* 0x101EC0 D3DDevice_Begin */
    D3DB_DATA2F,            /* 0x101E20 D3DDevice_SetVertexData2f */
    D3DB_DATA4F,            /* 0x101E60 D3DDevice_SetVertexData4f */
    D3DB_END,               /* 0x101F00 D3DDevice_End */
    D3DB_COPYRECTS,         /* 0x0FF580 D3DDevice_CopyRects */
    D3DB_VS_CONSTANT,       /* 0x102AA0 D3DDevice_SetVertexShaderConstant */
    D3DB_TEX_LOCK,          /* 0x103C80 D3DTexture_LockRect */
    D3DB_SURF_LOCK,         /* 0x103E00 D3DSurface_LockRect */
    D3DB_FAST_TEXTURE,      /* 0x0FEC00 3-dword push (the game's SetTexture fast path) */
    D3DB_PROBE_FD680,       /* 0x0FD680 returns 1 */
    D3DB_PROBE_FEC90,       /* 0x0FEC90 stores device+0x2430 */
    D3DB_PROBE_VBLANK_CB,   /* 0x039910 the callback the game passes to 0xFEC90 */
    D3DB_BEGIN_SB,          /* 0x1012CC BeginStateBlock (probable) */
    D3DB_END_SB,            /* 0x10191E EndStateBlock (probable) */
    D3DB_APPLY_SB,          /* 0x101705 ApplyStateBlock */
    D3DB_DELETE_SB,         /* 0x10188D DeleteStateBlock */
    D3DB_PROBE_DXT1NOISE,   /* 0x0FDC90 SetRenderState_Dxt1NoiseEnable (probable) */
    D3DB_PROBE_SPLASH,      /* 0x0875A0 the splash routine: a census phase mark */
    D3DB_COUNT
};

/* A wrapped call's arguments and registers, read on entry. */
typedef struct {
    uint32_t arg[6];
    uint32_t ecx, edx;
    uint32_t ret;                   /* the guest return address */
    unsigned long tid;
} d3db_args_t;

extern int d3db_on;                 /* RECOMP_D3D_BRIDGE or RECOMP_D3D_CENSUS set and not "0" */
extern int d3db_host_on;            /* RECOMP_D3D_BRIDGE: the host mirror */
extern int d3db_census_on;          /* RECOMP_D3D_CENSUS */

/* From recomp_manual.c: whether the calling thread owns a guest critical
 * section, from the cooperative-suspend bookkeeping. */
int coop_holds_cs(uint32_t cs_va);
void coop_watch_cs(uint32_t cs_va);     /* count this section's owners across threads */
int coop_cs_holders(void);              /* how many threads own the watched section now */

void d3db_init(void);
void d3db_enter(int id, d3db_args_t *a);
void d3db_leave(int id, d3db_args_t *a);
void d3db_mark(const char *label);  /* close the census interval at a phase boundary */
void d3db_memory_freed(uint32_t va, uint32_t bytes);   /* MmFreeContiguousMemory, bridge on (step 52) */

/* A wrapper body: the lifted function first, then (bridge on) the mirror. */
#define D3DB_CALL(id, gen) do {                 \
        d3db_args_t a_;                         \
        if (!d3db_on) { gen(); break; }         \
        d3db_enter((id), &a_);                  \
        gen();                                  \
        d3db_leave((id), &a_);                  \
    } while (0)

/* SetVertexData2f/4f run once per attribute per vertex (S4 part 2): the
 * mirror only stores the values (d3db_vertex_data, a few stores), and the
 * enter/leave pair runs for the census alone. */
void d3db_vertex_data(int id);
#define D3DB_DATA_CALL(id, gen) do {            \
        if (d3db_host_on)                       \
            d3db_vertex_data(id);               \
        if (d3db_census_on)                     \
            D3DB_CALL((id), gen);               \
        else                                    \
            gen();                              \
    } while (0)

#endif
