#!/bin/sh
# Build and run every engine self-test.
#   sh tests/run_selftests.sh [<out dir>]      (from the engine root; default out dir: build_selftest/)
# Needs MSYS2 mingw64 gcc + SDL2 (the GPU / presenter tests open a hidden window and need OpenGL 3.3).
# Exit code = number of failed tests.
set -u
cd "$(dirname "$0")/.." || exit 99
PATH=/c/msys64/mingw64/bin:$PATH
OUT=${1:-build_selftest}
mkdir -p "$OUT/gen" || exit 99
cp -f /c/msys64/mingw64/bin/SDL2.dll "$OUT/" 2>/dev/null
R=recompiler/src
RUN="-I runner -I runner/include"
SDL="-IC:/msys64/mingw64/include/SDL2"
RUNNER="runner/glue.c runner/coverage_route.c runner/hud.c runner/art.c runner/safe_file.c runner/video/sms_vdp.c \
runner/audio/sn76489.c runner/audio/ym2413.c runner/audio/emu2413.c runner/external/superzazu/z80.c"
CC="gcc -O1 -std=c11"
fails=0

# t <name> <exe args...> -- <gcc args...>
t() {
    name=$1; shift; args=""
    while [ "$1" != "--" ]; do args="$args $1"; shift; done; shift
    if ! $CC "$@" -o "$OUT/$name.exe" >"$OUT/$name.build.log" 2>&1; then
        echo "FAIL $name (build, see $OUT/$name.build.log)"; fails=$((fails + 1)); return
    fi
    if (cd "$OUT" && ./"$name.exe" $args) >"$OUT/$name.log" 2>&1; then
        echo "ok   $name: $(tail -n 1 "$OUT/$name.log" | cut -c1-110)"
    else
        echo "FAIL $name (run, see $OUT/$name.log)"; fails=$((fails + 1))
    fi
}

t z80_decoder -- -I $R tests/z80_decoder_selftest.c $R/z80_decoder.c
t z80_ops -- -I runner/include tests/z80_ops_selftest.c
t frontend -- -I $R tests/frontend_selftest.c $R/function_finder.c $R/z80_decoder.c $R/rom_parser.c
if $CC -I runner/audio $RUN -DPSG_BUILD_REFERENCE -c tests/psg_block_selftest.c -o "$OUT/psgref.o" >"$OUT/psg_block.build.log" 2>&1; then
    t psg_block -- -I runner/audio $RUN tests/psg_block_selftest.c runner/audio/sn76489.c "$OUT/psgref.o"
else echo "FAIL psg_block (reference build)"; fails=$((fails + 1)); fi
t vdp_jobs -- $RUN -I runner/video tests/vdp_jobs_selftest.c runner/video/sms_vdp.c
t bus bus_rom.bin -- $RUN -DSMS_PSG_REFERENCE tests/bus_selftest.c $RUNNER -lgdiplus -lole32

# Two stages: the recompiler emits a synthetic bank fixture, then the generated C is run.
if $CC -I $R -DSMS_TEST_GENERATE tests/generator_bank_selftest.c $R/code_generator.c $R/function_finder.c \
        $R/z80_decoder.c $R/rom_parser.c $R/game_config.c $R/toml.c -o "$OUT/generate.exe" >"$OUT/generate.build.log" 2>&1 &&
   "$OUT/generate.exe" "$OUT/gen" >"$OUT/generate.log" 2>&1; then
    G="$RUN -I $OUT/gen"
    t generator_bank -- $G tests/generator_bank_selftest.c "$OUT/gen/bankfixture_full.c" "$OUT/gen/bankfixture_dispatch.c"
    t native_spans -- -DSMS_NATIVE_HOOKS $G tests/native_spans_selftest.c
else echo "FAIL generate (fixture, see $OUT/generate.log)"; fails=$((fails + 1)); fi
t native_hooks -- -DSMS_NATIVE_HOOKS $RUN tests/native_hooks_selftest.c

t gpu_jobs -- $RUN -I runner/video $SDL -DSMS_GPU tests/gpu_jobs_selftest.c runner/host_gl.c runner/video/sms_vdp.c -lSDL2 -lopengl32
t reference_present -- $RUN $SDL -DSDL_MAIN_HANDLED -DSMS_HAVE_SDL -DSMS_GPU -DSMS_VDP_REFERENCE -DSMS_PSG_REFERENCE \
    tests/reference_present_selftest.c runner/host_sdl.c runner/host_gl.c $RUNNER -lSDL2 -lopengl32 -lgdiplus -lole32

echo "$fails failed"
exit $fails
