#ifndef BIOS_LOADER_H
#define BIOS_LOADER_H

#include <string>
#include <vector>

/** Copy current linked `bios` / `bios_logo` once; call before any ROM swap. */
void biosLoaderInit(void);

/**
 * biosMode:
 *   0 — embedded backup
 *   1 — C-BIOS intl: cbios_main_msx1.rom + cbios_basic / cbios_logo @ 8000h (EU order)
 *   2 — Philips VG8020: vg8020_basic-bios1.rom @ 0000h–7FFFh
 *   3 — C-BIOS main+logo: cbios_main_msx1.rom + cbios_logo_msx1.rom @ 8000h
 *   4 — Sony HB-10: hb-10_basic-bios1.rom @ 0000h–7FFFh
 *   5 — C-BIOS JP: cbios_main_msx1_jp.rom + logo/basic @ 8000h (JP order)
 *
 * Returns false if a file-only mode failed to load its main ROM (after falling back to embedded).
 */
bool biosLoaderApply(unsigned biosMode, const std::vector<std::string>& searchDirs);

#endif
