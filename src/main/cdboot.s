# PSX-iTests - BIOS call stubs for the tonyhax-style CD-R boot path.
#
# Direct port of tonyhax-international's bios-asm.S (alex-free/tonyhax).
# Each stub loads the BIOS function number into $t1 ($9) and jumps to the
# BIOS dispatch vector at 0xA0 / 0xB0 / 0xC0. The dispatch reads $t1,
# indexes the corresponding function table, and tail-calls the function,
# which then returns directly to our caller. Arguments/results pass in
# $a0..$a3 / $v0 per the normal calling convention.
#
# Reordering is left ON (no .set noreorder) so the assembler fills the
# jump delay slots automatically, exactly as tonyhax's source does.

.text
.align 4

# --- syscalls ---

.global cdbootEnterCritical
cdbootEnterCritical:
	li $4, 0x01
	syscall
	jr $31

.global cdbootExitCritical
cdbootExitCritical:
	li $4, 0x02
	syscall
	jr $31

# --- the fake EnqueueCdIntr used to skip CD re-init inside SetConf ---
# (prevents the CD interrupt handler being registered twice during
# bios_reinitialize)

.global cdbootFakeEnqueueCdIntr
cdbootFakeEnqueueCdIntr:
	lw $31, 0x14($29)
	addi $29, 0x18
	jr $31

# --- A-functions (j 0xA0) ---

.global cdbootDoExecute
cdbootDoExecute:
	li $17, 0
	li $18, 0
	li $19, 0
	li $20, 0
	li $21, 0
	li $22, 0
	li $9, 0x43
	j 0xA0

.global cdbootFlushCache
cdbootFlushCache:
	li $9, 0x44
	j 0xA0

.global cdbootInitVectors
cdbootInitVectors:
	li $9, 0x45
	j 0xA0

.global cdbootLoadAndExecute
cdbootLoadAndExecute:
	li $9, 0x51
	j 0xA0

.global cdbootCdInit
cdbootCdInit:
	li $9, 0x54
	j 0xA0

.global cdbootSetConf
cdbootSetConf:
	li $9, 0x9C
	j 0xA0

.global cdbootSetMemSize
cdbootSetMemSize:
	li $9, 0x9F
	j 0xA0

.global cdbootCdReadSector
cdbootCdReadSector:
	li $9, 0xA5
	j 0xA0

# --- B-functions (j 0xB0) ---

.global cdbootSetDefaultExit
cdbootSetDefaultExit:
	li $9, 0x18
	j 0xB0

.global cdbootFileOpen
cdbootFileOpen:
	li $9, 0x32
	j 0xB0

.global cdbootFileRead
cdbootFileRead:
	li $9, 0x34
	j 0xB0

.global cdbootFileClose
cdbootFileClose:
	li $9, 0x36
	j 0xB0

.global cdbootGetLastError
cdbootGetLastError:
	li $9, 0x54
	j 0xB0

.global cdbootGetC0Table
cdbootGetC0Table:
	li $9, 0x56
	j 0xB0

.global cdbootGetB0Table
cdbootGetB0Table:
	li $9, 0x57
	j 0xB0

# --- C-functions (j 0xC0) ---

.global cdbootInstallExceptionHandlers
cdbootInstallExceptionHandlers:
	li $9, 0x07
	j 0xC0

.global cdbootInstallDevices
cdbootInstallDevices:
	li $9, 0x12
	j 0xC0

.global cdbootAdjustA0Table
cdbootAdjustA0Table:
	li $9, 0x1C
	j 0xC0
