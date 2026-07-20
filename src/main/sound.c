/*
 * ps1-ram-tester - UI sound effects
 *
 * Uploads a single one-shot .VAG sample (the standard PS1 SPU-ADPCM format)
 * to SPU RAM once, then plays it on either of two dedicated channels
 * depending on whether it's triggered by menu scrolling or confirming a
 * selection - both share the same underlying sample data, just on
 * different channels, so triggering one never cuts the other off early.
 *
 * Built entirely on top of this project's own common/spu.c driver and
 * ps1/registers.h definitions, no external SDK involved.
 */

#include <stdbool.h>
#include <stdint.h>
#include "common/spu.h"
#include "main/sound.h"
#include "ps1/registers.h"

// The pickup.vag data embedded via addBinaryFile() in CMakeLists.txt.
extern const uint8_t uiSound[];
// The bgm.vag data embedded via addBinaryFile() in CMakeLists.txt - a
// 20-second looping excerpt (the full track doesn't fit in SPU RAM
// alongside the UI sounds; see the ADSR2/loop flag comments in playBGM()
// for how the looping itself works).
extern const uint8_t bgmSound[];
// The spu.vag data embedded via addBinaryFile() in CMakeLists.txt - a
// dedicated tone used only by the SPU channel test, distinct from the
// scroll/confirm UI sound.
extern const uint8_t spuTestSound[];

#define SCROLL_CHANNEL  0
#define CONFIRM_CHANNEL 1
#define BGM_CHANNEL     2

// Background music plays at half the volume of the UI sound effects.
#define BGM_VOLUME (SPU_MAX_VOLUME / 2)

// Standard 48-byte .VAG header. All multi-byte fields are big-endian.
typedef struct {
	char     id[4];        // "VAGp"
	uint32_t version;
	uint32_t reserved;
	uint32_t dataSize;
	uint32_t sampleRate;
	uint8_t  reserved2[12];
	char     name[16];
} VAGHeader;

static uint32_t swapEndian(uint32_t value) {
	return ((value & 0x000000ff) << 24)
	     | ((value & 0x0000ff00) <<  8)
	     | ((value & 0x00ff0000) >>  8)
	     | ((value & 0xff000000) >> 24);
}

static uint32_t uiSoundOffset      = 0;
static uint32_t bgmSoundOffset     = 0;
static uint32_t spuTestSoundOffset = 0;

static bool bgmEnabled = true;
static bool sfxEnabled = true;

void initSound(void) {
	const VAGHeader *uiHeader = (const VAGHeader *) uiSound;

	uint32_t uiDataSize    = swapEndian(uiHeader->dataSize);
	// SPU DMA transfers are done in 64-byte blocks.
	uint32_t uiAlignedSize = (uiDataSize + 63) & ~((uint32_t) 63);

	uiSoundOffset = SPU_RAM_ALLOC_OFFSET;
	sendSPURAMData(uiSound + sizeof(VAGHeader), uiSoundOffset, uiAlignedSize);
	waitForSPUDMADone();

	// Place the BGM data right after the UI sound, so nothing overlaps.
	const VAGHeader *bgmHeader = (const VAGHeader *) bgmSound;

	uint32_t bgmDataSize    = swapEndian(bgmHeader->dataSize);
	uint32_t bgmAlignedSize = (bgmDataSize + 63) & ~((uint32_t) 63);

	bgmSoundOffset = uiSoundOffset + uiAlignedSize;
	sendSPURAMData(bgmSound + sizeof(VAGHeader), bgmSoundOffset, bgmAlignedSize);
	waitForSPUDMADone();

	// Place the SPU test tone right after the BGM data.
	const VAGHeader *spuTestHeader = (const VAGHeader *) spuTestSound;

	uint32_t spuTestDataSize    = swapEndian(spuTestHeader->dataSize);
	uint32_t spuTestAlignedSize = (spuTestDataSize + 63) & ~((uint32_t) 63);

	spuTestSoundOffset = bgmSoundOffset + bgmAlignedSize;
	sendSPURAMData(
		spuTestSound + sizeof(VAGHeader), spuTestSoundOffset, spuTestAlignedSize
	);
	waitForSPUDMADone();
}

static void playSample(
	const uint8_t *soundData,
	uint32_t      soundOffset,
	int           channel
) {
	const VAGHeader *header = (const VAGHeader *) soundData;

	uint32_t sampleRate = swapEndian(header->sampleRate);
	uint32_t pitch       = (sampleRate << 12) / 44100;

	// Stop the channel first in case it's still playing from a previous
	// trigger, then reconfigure and key it back on.
	if (channel < 16)
		SPU_KOFF0 = 1 << channel;
	else
		SPU_KOFF1 = 1 << (channel - 16);

	SPU_CH_VOLL (channel) = SPU_MAX_VOLUME;
	SPU_CH_VOLR (channel) = SPU_MAX_VOLUME;
	SPU_CH_PITCH(channel) = pitch;
	SPU_CH_SSA  (channel) = soundOffset / SPU_RAM_ADDR_UNIT;
	// Instant attack, slowest decay, max sustain level: holds at full
	// volume until the sample's own end-of-data flag stops it.
	SPU_CH_ADSR1(channel) = 0x00ff;
	SPU_CH_ADSR2(channel) = 0x0000;

	if (channel < 16)
		SPU_KON0 = 1 << channel;
	else
		SPU_KON1 = 1 << (channel - 16);
}

void playScrollSound(void) {
	if (sfxEnabled)
		playSample(uiSound, uiSoundOffset, SCROLL_CHANNEL);
}

void playConfirmSound(void) {
	if (sfxEnabled)
		playSample(uiSound, uiSoundOffset, CONFIRM_CHANNEL);
}

void playTestTone(int channel) {
	playSample(spuTestSound, spuTestSoundOffset, channel);
}

void playBGM(void) {
	const VAGHeader *header = (const VAGHeader *) bgmSound;

	uint32_t sampleRate = swapEndian(header->sampleRate);
	uint32_t pitch       = (sampleRate << 12) / 44100;

	SPU_KOFF0 = 1 << BGM_CHANNEL;

	SPU_CH_VOLL (BGM_CHANNEL) = BGM_VOLUME;
	SPU_CH_VOLR (BGM_CHANNEL) = BGM_VOLUME;
	SPU_CH_PITCH(BGM_CHANNEL) = pitch;
	SPU_CH_SSA  (BGM_CHANNEL) = bgmSoundOffset / SPU_RAM_ADDR_UNIT;
	// Loop start address: where the SPU jumps back to once it reaches the
	// End+Repeat flag encoded on the sample's final ADPCM block (see
	// wav2vag.py's --loop option). Pointing it at the sample's own start
	// makes the whole clip loop seamlessly, entirely in hardware - no
	// per-frame code needed anywhere else in the project.
	SPU_CH_LSAX (BGM_CHANNEL) = bgmSoundOffset / SPU_RAM_ADDR_UNIT;
	SPU_CH_ADSR1(BGM_CHANNEL) = 0x00ff;
	SPU_CH_ADSR2(BGM_CHANNEL) = 0x0000;

	SPU_KON0 = 1 << BGM_CHANNEL;

	// playBGM() always makes BGM audible - keep the tracked enabled flag
	// in sync, otherwise a later toggleBGM() call can desync from the
	// real audio state (this was a real bug: a pause/restore cycle via
	// toggleBGM()+playBGM() left bgmEnabled stuck at false even though
	// BGM was audibly playing, so a second such cycle silently skipped
	// pausing it at all).
	bgmEnabled = true;
}

bool toggleBGM(void) {
	bgmEnabled = !bgmEnabled;

	// Mute/restore volume directly rather than stopping/restarting
	// playback - the SPU keeps looping it in hardware regardless, this
	// just silences the channel without touching SPU RAM or the loop
	// state at all.
	uint16_t volume = bgmEnabled ? BGM_VOLUME : 0;
	SPU_CH_VOLL(BGM_CHANNEL) = volume;
	SPU_CH_VOLR(BGM_CHANNEL) = volume;

	return bgmEnabled;
}

bool isBGMEnabled(void) {
	return bgmEnabled;
}

bool toggleSFX(void) {
	sfxEnabled = !sfxEnabled;
	return sfxEnabled;
}

bool isSFXEnabled(void) {
	return sfxEnabled;
}
