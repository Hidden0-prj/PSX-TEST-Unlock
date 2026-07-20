/*
 * PSX-iTests - CD-R/import disc boot via low-level reads (no BIOS CdInit).
 *
 * Confirmed on hardware:
 *  - SecretUnlock 0x50..0x56 works (drive readable after unlock+lid, our
 *    low-level ReadN returned INT1 with real data).
 *  - The BIOS CdInit() call HANGS in this bare-metal environment (it is
 *    interrupt-driven; our polled reads work fine).
 *  - PSX-iTests is now linked at 0x80180000 (see cmake/executable.ld) so
 *    the game's normal 0x80010000 load region is free.
 *
 * Approach (avoids the hanging BIOS CdInit entirely):
 *  1. Unlock the drive (SecretUnlock), lid open/close.
 *  2. Our own low-level Init + SetMode.
 *  3. Low-level read of the ISO9660 PVD -> root dir -> SYSTEM.CNF to find
 *     the boot EXE and kernel config (TCB/EVENT/STACK).
 *  4. Low-level read of the EXE header + body straight into its load
 *     address (0x80010000, now free).
 *  5. bios_reinitialize() + SetConf() to hand the kernel to the game in a
 *     clean state, then DoExecute(). The game's OWN CdInit then works
 *     because it runs after we're gone (same as tonyhax's model).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "main/cdboot.h"
#include "main/defs.h"
#include "main/font.h"
#include "main/mainmenu.h"
#include "main/renderer.h"
#include "main/ui.h"
#include "ps1/registers.h"

/* ---- BIOS calls (stubs in cdboot.s) ---- */
void    cdbootEnterCritical(void);
void    cdbootExitCritical(void);
void    cdbootFakeEnqueueCdIntr(void);
void    cdbootDoExecute(const void *offsets, uint32_t p1, uint32_t p2);
void    cdbootFlushCache(void);
void    cdbootInitVectors(void);
void    cdbootSetConf(uint32_t evcb, uint32_t tcb, void *stacktop);
void    cdbootSetMemSize(uint32_t size);
void    cdbootSetDefaultExit(void);
void    cdbootInstallExceptionHandlers(void);
void    cdbootInstallDevices(uint32_t tty);
void    cdbootAdjustA0Table(void);

static void ** const BIOS_A0_TABLE = (void **) 0x200;

/* ---- CD registers (KSEG1 uncached) ---- */
#define CDR0 (*(volatile uint8_t *) 0xBF801800)
#define CDR1 (*(volatile uint8_t *) 0xBF801801)
#define CDR2 (*(volatile uint8_t *) 0xBF801802)
#define CDR3 (*(volatile uint8_t *) 0xBF801803)
#define CD_DATA_FIFO ((volatile uint32_t *) 0xBF801802)

/* ---- status display ---- */
static void status(RenderContext *ctx, const char *msg) {
    for (int i = 0; i < 3; i++) {
        beginFrame(ctx);
        renderProgressScreen(ctx, 0, 1, msg);
        endFrame(ctx);
    }
}
static void status_hold(RenderContext *ctx, const char *msg) {
    for (int i = 0; i < 75; i++) {
        beginFrame(ctx);
        renderProgressScreen(ctx, 0, 1, msg);
        endFrame(ctx);
    }
}

/* ---- low-level CD command layer (polled; proven working) ---- */

static void cd_command(uint8_t cmd, const uint8_t *params, uint8_t len) {
    while (CDR0 & 0x80) ;      // BUSYSTS
    CDR0 = 0;
    CDR3 = 0xC0;               // flush FIFOs
    while (len--) CDR2 = *params++;
    CDR0 = 1;
    CDR2 = 0x00;               // no controller IRQ (we poll)
    CDR3 = 0x07;               // ack
    CDR0 = 0;
    CDR1 = cmd;
}

static void cd_command_race(uint8_t cmd) {
    CDR0 = 0;
    CDR3 = 0xC0;
    CDR0 = 1;
    CDR2 = 0x00;
    CDR3 = 0x07;
    CDR0 = 0;
    CDR1 = cmd;
}

static uint8_t cd_wait_int(void) {
    uint32_t t = 0x01000000;
    while ((CDR0 & 0x80) && --t) ;
    if (!t) return 0xFF;
    CDR0 = 1;
    uint8_t f;
    t = 0x01000000;
    do { f = CDR3 & 0x07; } while (f == 0 && --t);
    if (!t) return 0xFF;
    CDR3 = 0x07;
    return f;
}

static uint8_t cd_read_reply(uint8_t *buf) {
    CDR0 = 1;
    uint8_t n = 0;
    while (CDR0 & 0x20) { buf[n++] = CDR1; if (n >= 16) break; }
    return n;
}

static void cd_drive_init(void) {
    cd_command_race(0x0A);
    cd_wait_int();
    cd_wait_int();
}

static uint8_t cd_getstat(void) {
    cd_command(0x01, NULL, 0);
    cd_wait_int();
    uint8_t r[16];
    cd_read_reply(r);
    return r[0];
}

/* ---- unlock ---- */

static bool backdoor_cmd(uint8_t cmd, const char *str) {
    uint8_t len = 0;
    if (str) { const char *p = str; while (*p) { len++; p++; } }
    cd_command(cmd, (const uint8_t *) str, len);
    if (cd_wait_int() != 5) return false;
    uint8_t reply[16];
    if (cd_read_reply(reply) != 2) return false;
    if (!(reply[0] & 0x01)) return false;
    return reply[1] == 0x40;
}

static bool unlock_drive(void) {
    return
        backdoor_cmd(0x50, NULL) &&
        backdoor_cmd(0x51, "Licensed by") &&
        backdoor_cmd(0x52, "Sony") &&
        backdoor_cmd(0x53, "Computer") &&
        backdoor_cmd(0x54, "Entertainment") &&
        backdoor_cmd(0x55, "of America") &&
        backdoor_cmd(0x56, NULL);
}

/* ---- low-level sector reading (PIO) ---- */

static uint8_t to_bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static void cd_setmode_data(void) {
    uint8_t mode = 0x00;   // single speed, 2048-byte data sectors
    cd_command(0x0E, &mode, 1);
    cd_wait_int();
}

// Reads 'sectors' consecutive 2048-byte sectors starting at LBA into dest.
// Returns true on success.
static bool cd_read(uint32_t lba, uint32_t sectors, uint8_t *dest) {
    uint32_t tot = lba + 150;
    uint8_t loc[3] = { to_bcd(tot / 4500), to_bcd((tot % 4500) / 75), to_bcd(tot % 75) };
    cd_command(0x02, loc, 3);          // SetLoc
    if (cd_wait_int() != 3) return false;

    cd_command(0x06, NULL, 0);         // ReadN
    if (cd_wait_int() != 3) return false;

    bool ok = true;
    for (uint32_t s = 0; s < sectors; s++) {
        if (cd_wait_int() != 1) { ok = false; break; }   // INT1 = sector ready
        CDR0 = 0;
        CDR3 = 0x80;                    // BFRD: request data
        uint32_t t = 0x00100000;
        while (!(CDR0 & 0x40) && --t) ; // wait DRQSTS
        uint32_t *d = (uint32_t *) (dest + s * 2048);
        for (int w = 0; w < 512; w++) d[w] = *CD_DATA_FIFO;
        CDR3 = 0x00;                    // clear BFRD
    }

    cd_command(0x09, NULL, 0);          // Pause
    cd_wait_int();
    cd_wait_int();
    return ok;
}

/* ---- ISO9660 ---- */

// Case-insensitive match of an ISO directory entry name (which includes
// the ";1" version) against 'want'.
static bool name_match(const char *want, const char *nm, uint8_t nlen) {
    int i = 0;
    for (; i < nlen; i++) {
        char a = want[i];
        char b = nm[i];
        if (a == 0) return false;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return false;
    }
    return want[i] == 0;
}

// Searches a directory buffer for 'name', returns its extent LBA (0 if not
// found) and fills *size.
static uint32_t iso_find(const char *name, const uint8_t *dir, uint32_t dirsize, uint32_t *size) {
    uint32_t off = 0;
    while (off + 33 < dirsize) {
        uint8_t len = dir[off];
        if (len == 0) {
            off = (off & ~2047u) + 2048;  // next sector
            continue;
        }
        uint8_t nlen = dir[off + 32];
        const char *nm = (const char *) &dir[off + 33];
        if (name_match(name, nm, nlen)) {
            *size = *(const uint32_t *) &dir[off + 10];
            return *(const uint32_t *) &dir[off + 2];
        }
        off += len;
    }
    return 0;
}

/* ---- SYSTEM.CNF parsing ---- */

static bool parse_boot_path(const char *cnf, char *out, int maxlen) {
    while (*cnf) {
        if (cnf[0]=='B'&&cnf[1]=='O'&&cnf[2]=='O'&&cnf[3]=='T') {
            const char *p = cnf + 4;
            while (*p==' '||*p=='\t') p++;
            if (*p!='=') { cnf++; continue; }
            p++;
            while (*p==' '||*p=='\t') p++;
            int i=0;
            while (*p && *p!='\r' && *p!='\n' && i<maxlen-1) out[i++]=*p++;
            out[i]=0;
            return i>0;
        }
        cnf++;
    }
    return false;
}

static bool parse_hex_key(const char *cnf, const char *key, uint32_t *out) {
    int klen=0; while (key[klen]) klen++;
    while (*cnf) {
        bool m=true;
        for (int i=0;i<klen;i++){ if(cnf[i]!=key[i]){m=false;break;} }
        if (m) {
            const char *p=cnf+klen;
            while (*p==' '||*p=='\t') p++;
            if (*p=='=') {
                p++; while (*p==' '||*p=='\t') p++;
                if (p[0]=='0'&&(p[1]=='x'||p[1]=='X')) p+=2;
                uint32_t v=0; bool any=false;
                for(;;){ char c=*p; uint32_t d;
                    if(c>='0'&&c<='9')d=c-'0';
                    else if(c>='a'&&c<='f')d=c-'a'+10;
                    else if(c>='A'&&c<='F')d=c-'A'+10;
                    else break;
                    v=(v<<4)|d; any=true; p++; }
                if (any){ *out=v; return true; }
            }
        }
        cnf++;
    }
    return false;
}

// Strips "cdrom:" and any leading '\' or '/' from a BOOT= path, leaving
// e.g. "SLUS_007.07;1".
static const char *strip_cdrom(const char *path) {
    const char *p = path;
    if (p[0]=='c'&&p[1]=='d'&&p[2]=='r'&&p[3]=='o'&&p[4]=='m'&&p[5]==':')
        p += 6;
    while (*p=='\\' || *p=='/') p++;
    return p;
}

/* ---- BIOS reinitialize (tonyhax) ---- */

static void *parse_warmboot_jal(uint32_t idx) {
    const uint32_t *wb = (const uint32_t *) BIOS_A0_TABLE[0xA0];
    uint32_t prefix = (uint32_t) wb & 0xF0000000;
    uint32_t suffix = (wb[idx] & 0x3FFFFFF) << 2;
    return (void *) (prefix | suffix);
}
static void bios_copy_relocated_kernel(void) { ((void(*)(void))parse_warmboot_jal(12))(); }
static void bios_copy_a0_table(void)         { ((void(*)(void))parse_warmboot_jal(14))(); }

static void bios_reinitialize(void) {
    cdbootEnterCritical();
    bios_copy_relocated_kernel();
    bios_copy_a0_table();
    cdbootInitVectors();
    cdbootAdjustA0Table();
    cdbootInstallExceptionHandlers();
    cdbootSetDefaultExit();
    IRQ_STAT = 0;
    IRQ_MASK = 0;
    cdbootInstallDevices(0);
    void *real = BIOS_A0_TABLE[0xA2];
    BIOS_A0_TABLE[0xA2] = cdbootFakeEnqueueCdIntr;
    cdbootSetConf(0x10, 0x4, (void *) 0x801FFF00);
    BIOS_A0_TABLE[0xA2] = real;
    cdbootSetMemSize(8);
    cdbootExitCritical();
}

/* ---- PS-EXE header ---- */
typedef struct {
    char     sig[8];
    uint8_t  pad[8];
    void    *pc0;
    void    *gp0;
    void    *load_addr;
    uint32_t load_size;
    uint32_t res0[2];
    void    *bss_addr;
    uint32_t bss_size;
    void    *sp_base;
    uint32_t sp_off;
} exe_header_t;

/* buffers in PSX-iTests' own (high) RAM - do not overlap the game */
static uint8_t  g_sector[2048];
static uint8_t  g_rootdir[2048 * 12];
static char     g_cnf[2048];
static char     g_bootpath[64];

/* ---- entry ---- */

void bootCDR(RenderContext *ctx, UIState *state, const MenuItem *item) {
    (void) state; (void) item;

    // 1. Unlock.
    status(ctx, "Unlocking drive...");
    cd_drive_init();
    if (!unlock_drive()) {
        status_hold(ctx, "Unlock rejected - use Boot CD-ROM.");
        return;
    }

    // 2. Lid cycle so the unlocked drive re-reads the disc.
    status(ctx, "Open the CD lid...");
    while ((cd_getstat() & 0x10) != 0x10) { beginFrame(ctx); renderProgressScreen(ctx,0,1,"Open the CD lid..."); endFrame(ctx); }
    status(ctx, "Now close the CD lid...");
    while ((cd_getstat() & 0x10) != 0x00) { beginFrame(ctx); renderProgressScreen(ctx,0,1,"Now close the CD lid..."); endFrame(ctx); }

    // 3. Our own low-level init (NOT BIOS CdInit).
    status(ctx, "L1: drive init + set mode...");
    cd_drive_init();
    cd_setmode_data();

    // 4. Read ISO PVD (LBA 16), get root directory.
    status(ctx, "L2: reading volume descriptor...");
    if (!cd_read(16, 1, g_sector)) { status_hold(ctx, "FAIL: read PVD (LBA16)"); return; }
    uint32_t root_lba  = *(uint32_t *) &g_sector[156 + 2];
    uint32_t root_size = *(uint32_t *) &g_sector[156 + 10];
    if (root_lba == 0) { status_hold(ctx, "FAIL: bad root dir in PVD"); return; }

    uint32_t root_secs = (root_size + 2047) / 2048;
    if (root_secs > 12) root_secs = 12;
    status(ctx, "L3: reading root directory...");
    if (!cd_read(root_lba, root_secs, g_rootdir)) { status_hold(ctx, "FAIL: read root dir"); return; }

    // 5. SYSTEM.CNF -> BOOT path + kernel config.
    const char *bootfile = "PSX.EXE;1";
    uint32_t tcb = 0x4, event = 0x10, stacktop = 0x801FFF00;

    uint32_t cnf_size = 0;
    uint32_t cnf_lba = iso_find("SYSTEM.CNF;1", g_rootdir, root_secs * 2048, &cnf_size);
    if (cnf_lba) {
        status(ctx, "L4: reading SYSTEM.CNF...");
        if (cd_read(cnf_lba, 1, (uint8_t *) g_cnf)) {
            g_cnf[cnf_size < 2047 ? cnf_size : 2047] = 0;
            if (parse_boot_path(g_cnf, g_bootpath, sizeof(g_bootpath)))
                bootfile = strip_cdrom(g_bootpath);
            parse_hex_key(g_cnf, "TCB", &tcb);
            parse_hex_key(g_cnf, "EVENT", &event);
            parse_hex_key(g_cnf, "STACK", &stacktop);
        }
    }

    // 6. Find the boot EXE, read its header.
    { char m[64]; snprintf(m, sizeof(m), "L5: locating %s", bootfile); status(ctx, m); }
    uint32_t exe_size = 0;
    uint32_t exe_lba = iso_find(bootfile, g_rootdir, root_secs * 2048, &exe_size);
    if (exe_lba == 0) { status_hold(ctx, "FAIL: boot EXE not in root dir"); return; }

    if (!cd_read(exe_lba, 1, g_sector)) { status_hold(ctx, "FAIL: read EXE header"); return; }
    exe_header_t *h = (exe_header_t *) g_sector;
    if (h->sig[0] != 'P' || h->sig[1] != 'S') { status_hold(ctx, "FAIL: bad PS-EXE signature"); return; }

    uint8_t *load_addr = (uint8_t *) h->load_addr;
    uint32_t load_size = h->load_size;

    // 7. Read the EXE body straight to its load address (now free RAM).
    { char m[64]; snprintf(m, sizeof(m), "L6: loading %u KB @ %08X", (unsigned)(load_size/1024), (unsigned)(uint32_t)load_addr); status(ctx, m); }
    uint32_t body_secs = (load_size + 2047) / 2048;
    if (!cd_read(exe_lba + 1, body_secs, load_addr)) { status_hold(ctx, "FAIL: read EXE body"); return; }

    // 8. Hand off: clean kernel, configure, DoExecute. The PS-EXE header
    //    from pc0 onward IS the EXEC struct DoExecute wants
    //    (pc0,gp0,t_addr,t_size,d_addr,d_size,b_addr,b_size,s_addr,s_size).
    //    Patch the stack pointer in, as the BIOS boot does.
    status(ctx, "L7: starting game...");

    h->sp_base = (void *) stacktop;
    h->sp_off  = 0;

    bios_reinitialize();
    cdbootEnterCritical();
    cdbootSetConf(event, tcb, (void *) stacktop);
    cdbootFlushCache();
    cdbootDoExecute(&h->pc0, 0, 0);

    status_hold(ctx, "DoExecute returned - boot failed.");
}
