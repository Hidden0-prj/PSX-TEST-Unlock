#pragma once

#include "main/renderer.h"
#include "main/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Attempts to unlock the CD-R/import drive and then boot the disc
 * currently inserted, WITHOUT performing a hardware reset.
 *
 * Replicates the working sequence from tonyhax-international:
 *   1. Send the SecretUnlock commands 0x50..0x56 to unlock the drive.
 *   2. Call CdInit() via the BIOS A0 table so the BIOS CD filesystem
 *      knows the disc is now unlocked (no reset - a reset re-locks).
 *   3. Open cdrom:SYSTEM.CNF;1 to find the actual boot executable.
 *   4. Fall back to cdrom:PSX.EXE;1 if SYSTEM.CNF is missing.
 *   5. Call LoadAndExecute() via the BIOS A0 table, which loads the
 *      game EXE and hands control to it.
 *
 * Does not return on success.  Returns normally if the drive unlock,
 * CdInit, or file access fails (caller can display an error and retry).
 *
 * Only for US consoles.  The region string "of America" is hard-coded,
 * matching the "for U/C" console region already confirmed on the target
 * hardware.  Japanese consoles (which return "for Japan") do not support
 * the 0x50..0x56 commands at all.
 */
void bootCDR(RenderContext *ctx, UIState *state, const MenuItem *item);

#ifdef __cplusplus
}
#endif
