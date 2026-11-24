/* interp_only.c - the one piece of generated code a runner needs, for a GENERIC build without a game.
 *
 * A runner built without SMSGG_HAVE_GAME_LAYOUT (no <prefix>_*.c from the recompiler) runs every ROM in
 * the superzazu interpreter (main.c forces --interp there). Nothing ROM-specific is compiled in, so the
 * exe can be shipped; its use is the launcher's title-screen capture (--title-shot) before a game has
 * been built. call_by_address() is the recompiled path's entry and is never reached in that mode.
 */
#include "sms_runtime.h"
#include <stdio.h>
#include <stdlib.h>

void call_by_address(uint16_t addr)
{
    fprintf(stderr, "[runner] generic interpreter build: no recompiled code (call_by_address %04X)\n", addr);
    abort();
}
