#ifndef BIOS_LOADER_H
#define BIOS_LOADER_H

#include <string>
#include <vector>

/** Copy current linked `bios` / `bios_logo` once; call before any ROM swap. */
void biosLoaderInit(void);

/**
 * biosMode:
 *   0 — embedded backup (J/K font still forces mode 1 so JP/KR ROMs get file BIOS).
 *   1 — C-BIOS: main (cbios_main_msx1*.rom) + 8000h from cbios_basic / cbios_logo (order by region).
 *   2 — Philips VG8020: vg8020_basic-bios1.rom @ 0000h–7FFFh; 8000h from embedded backup.
 *   3 — openMSX C-BIOS layout: main + cbios_logo_msx1.rom @ 8000h (no cbios_basic.rom).
 *
 * Returns false if a file-only mode failed to load its main ROM (after falling back to embedded).
 */
bool biosLoaderApply(unsigned biosMode, char font, const std::vector<std::string>& searchDirs);

#endif
