/*
 * ym2413.h - YM2413 (OPLL) FM sound unit for the SMS (Japanese Mark III / FM Sound Unit
 * and the SEGA AGES / Switch releases that add FM music to Western ROMs).
 *
 * Ports: $F0 = register address, $F1 = register data, $F2 = audio control
 * (bit0: FM output enabled, bit1: PSG output disabled; reads back the written value so
 * games/stubs can detect the unit). The OPLL core is emu2413 (MIT, Mitsutaka Okazaki,
 * vendored as emu2413.c/h). The FM output is mixed into the PSG sample stream (PSG
 * rate = Z80 clock / 16) through the PSG's aux-mixer hook.
 */
#ifndef AUDIO_YM2413_H
#define AUDIO_YM2413_H
#include <stdint.h>
#include <stddef.h>

void    fm_reset(void);
void    fm_port_write(uint8_t port, uint8_t value);   /* port = $F0..$F3 */
uint8_t fm_port_read(uint8_t port);
/* Aux mixer callback for psg_set_aux_mixer(): `frames` = n interleaved stereo S16 frames
 * holding the PSG output at PSG rate; FM is added (and PSG optionally muted per $F2). */
void    fm_mix(int16_t *frames, size_t n);
/* Per-channel output gain, 0..256 (256 = unchanged): ch 0..8 melodic, 9..13 rhythm (BD, HH, SD,
 * TOM, CYM). Host-side mixing (volume options), not chip state. */
void    fm_set_channel_gain(unsigned ch, unsigned gain);
#endif
