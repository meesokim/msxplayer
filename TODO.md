# TODO

## Video / VRAM coherence (open)

**Status:** Not fully resolved — improved vs. earlier builds, still not satisfactory on VRAM-heavy games (e.g. fast pixel movement / bottom-of-screen shake).

**Context:**

- Host display should stay coherent with VDP VRAM (no visible tear or “half-updated” frames).
- Approaches already in the tree include: VRAM+regs+palette snapshot for `msx1RenderFrameToRgb565`, skipping `sync()` in VDP port `0x98`/`0x99` paths while that snapshot runs (`vdp_msxplay_coherent_frame_grab`), driving `RefreshScreen` from blueberry `onDisplay()` at emulated vblank (removed duplicate call from the main loop), and ~30 Hz host present decimation.

**Still to explore / verify:**

- Tighter alignment with openMSX-style renderer sync (`renderUntil` / per-VRAM-write catch-up) if we keep a custom raster path.
- Optional strict “paint only when `vdpGetDrawArea()==0`” or configurable 60 Hz host refresh.
- Whether MSX1 VRAM access timing / wait states need modeling for specific titles.
- Profiling: confirm remaining artifact is temporal (frame phase) vs. still a double-read path somewhere.

Add findings and closed items here when this is considered fixed.
