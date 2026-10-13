# Testing

What the port's tests are and how they are wired: the test framework and its labels, every test
program by area, the render gates over saved frames, the tests that need the player's disc, the
environment switches that force GPU paths, the frame dumps and the
[replay tool](ARCHITECTURE.md#terms), [pad scripts](ARCHITECTURE.md#terms) and
[traces](ARCHITECTURE.md#terms), the developer build helper, and CI. How to run `ctest` is
[../BUILDING.md](../BUILDING.md#tests)'s.

## Files

| path | what it holds |
| --- | --- |
| `cmake/IcoTests.cmake` | the `add_test` wrapper (labels, the `tests` target), `ico_add_sharded_test` |
| `cmake/IcoShardCheck.cmake` | the `<name>.cases` check of a sharded test |
| `port/test/ico_check.h` | `CHECK`, `CHECK_STR`, `ico_check_summary` |
| `port/test/ico_shard.h` | the shard split of a slow test program |
| `port/*/test/`, `port/platform/fpenv_test.c` | the test programs and their helpers |
| `port/render/test/*.cmake` | script tests over the replay tool and the fog self-test |
| `tools/native_frame_check.py`, `tools/native_frame_dumps.txt`, `docs/native_frame_baseline.json` | the native-frame gates, their dump list and their baseline |
| `tools/coverage_check.py`, `tools/coverage_corpus.txt` | the regression renders and their corpus |
| `port/render/tools/rd_replay_tool.c` | the replay tool |
| `port/platform/hotkeys.h`, `dump_zip.h`, `port/render/rd_dump.c` | F12, the aspect sweep, the dump format |
| `port/input/pad_script.h`, `input_record.h`, `port/platform/trace_host.h` | pad scripts, recordings, the trace |
| `tools/dev_build.sh` | build a few targets of one checkout and run a few tests |
| `.github/workflows/ci.yml`, `ios.yml` | CI |

## The framework

`cmake/IcoTests.cmake` is included before the first `add_test` and wraps it: each test gets a
`LABELS` entry for the directory it is registered in, and the executable targets its command names
(the program itself, or `$<TARGET_FILE:x>` in an argument) are recorded (`cmake/IcoTests.cmake`,
`add_test`, `_ico_test_area`). The labels are `render` (`port/render`), `ui`, `audio`, `save` (their
directories), `game` (`port/game`, `port/math`, `port/null`, `port/test`), `data` (`port/data`,
`port/fmv`), `rhi` (`port/rhi`, `port/shaders`) and `platform` (`port/platform`, `port/input`,
`port/config` and the top level), plus `tools` on any test whose command runs a script under
`tools/`.

A test registered from a deferred call takes the area of its program's first source.
`ico_finalize_tests`, run deferred at the end of the top directory, marks every executable that is
a test program (a name ending in `_test`, or one a test command names) `EXCLUDE_FROM_ALL` and makes
the `tests` target depend on all of them; `rd_replay_tool`, `mc_import` and `ico_pc` stay in `all`
(`ICO_KEEP_IN_ALL`). A new test therefore needs only its `add_test` line. Tests run only where the
build can run them: a native build, a cross build with an emulator, or a cross build for the host's
own system (`CMakeLists.txt`, `ICO_RUN_TESTS`).

A test that needs something the machine may lack exits 77 and is registered with
`SKIP_RETURN_CODE 77`, which ctest reports as skipped; a script test prints `SKIP:` and is
registered with `SKIP_REGULAR_EXPRESSION` (`port/render/test/rd_replay_tool_list.cmake`). A C test
uses `port/test/ico_check.h`: `CHECK(cond)` or `CHECK(cond, fmt, ...)` prints
`FAIL file:line: ...` and counts, `CHECK_STR` compares two strings, and `ico_check_summary(name)`
prints `name: ok` or the failure count and returns the exit code. The Settings tests share
`port/ui/test/settings_fixture.h`, which has its own `CHECK` over the game's headers
([CONVENTIONS.md](CONVENTIONS.md#tests)).

A slow program lists its cases once with a weight in seconds (`IcoShardCase k_cases[]`), starts
`main` with `ico_shard_start` and wraps each case in `ico_shard_take("name")`. Cases go heaviest
first to the shard with the least weight so far, so ownership is a pure function of the table and
N; a run with no `--shard` owns every case. `ico_shard_take` stops the program when a call does not
match the table's next entry, and `ico_shard_finish` fails when a case was never reached, so the
table cannot drift from `main` (`port/test/ico_shard.h`, header comment).
`ico_add_sharded_test(<name> <N> PROGRAM <target> ...)` registers `<name>.1ofN` to `<name>.NofN`
and `<name>.cases`, which runs `--list-cases` for every N and checks through
`cmake/IcoShardCheck.cmake` that every case is in exactly one shard and no shard is empty
(`cmake/IcoTests.cmake`, `ico_add_sharded_test`). `rd_blur`, `rd_mesh`, `rd_mesh_mali` and
`rd_mesh_minlimits` are sharded four ways (`port/render/CMakeLists.txt`).

## Test programs by area

One line per program, from its first comment; the ctest names follow where they differ from the
program's. Variants (`_nodual`, `_mali`, `_minlimits`, `_storage`, `_fixed`, `_d24s8`) rerun a
program under an environment switch ([Environment switches](#environment-switches)).

### port/audio/test

- `adpcm_slots_test.c`: the ADPCM request daemon with both stream slots taken; a stream already
  fading out gives its slot up (`adpcm_slots`).
- `adpcm_test.c`: SPU ADPCM block decoding against hand-worked vectors (`adpcm`).
- `libsd_irx_test.c`: the LIBSD.IRX reader on a synthetic module, its structural checks and its
  fallback (`libsd_irx`).
- `mix_gain_test.c`: the music, effects and film gains, every fixed volume word unchanged at 100 %
  (`mix_gain`).
- `pan_test.c`: mirror mode's pan swap and the per-[vsync](ARCHITECTURE.md#terms) block size
  (`audio_pan`).
- `sndn2_test.c`: the SNDN2DRV host and the Sg sequencer against what the IRX's code does; reads
  the disc when present (`sndn2`).
- `spu2_bench.c`: the [SPU2](ARCHITECTURE.md#terms) render's cost, and with `--check` a CRC-32 of
  five 10 s scenes (`spu2_render_crc`).
- `spu2_test.c`: the software SPU2 and its [libsd](ARCHITECTURE.md#terms) front end: interpolation,
  ADSR, voices, reverb (`spu2`).
- `volume_test.c`: the output volume's mapping, clamp and integer scaling (`volume`).

### port/config/test, port/null/test, port/save/test

- `config_test.c`: `config.toml`'s reader and writer, round trips, the ini over `config.toml` over
  default precedence, the language answer, the wall clock and the [EE](ARCHITECTURE.md#terms) timers
  (`config`).
- `null_devices_test.c`: the pad as an empty console answers it (`null_devices`).
- `save_test.c`: the host memory card through the calls `mcard.c` makes, then `mcard.c` and
  `mcdata.c` themselves on the [fiber](ARCHITECTURE.md#terms) scheduler, then the card importer
  (`save`).

### port/data/test

- `archive_test.c`: the archive backend and the first-run extractor, on a synthetic image and on
  the disc (`archive_synthetic`, `archive_disc`).
- `iso_import_test.c`: the Android first start's copy of the chosen image over SDL memory streams
  (`iso_import`).
- `json_test.c`: `port/data/json.h`: numbers, strings, nesting, the depth limit, malformed input
  (`json`).
- `tables_test.c`: the runtime table loader against the PS2 build's tables generated from the base
  ELF (`tables_loader`, with `ICO_BASE_ELF`).
- `vfs_test.c`: the [VFS](ARCHITECTURE.md#terms) and its ISO9660 backend, the
  [libcdvd](ARCHITECTURE.md#terms), [SIF](ARCHITECTURE.md#terms) and [IOP](ARCHITECTURE.md#terms)
  RAM layers (`vfs_synthetic`, `vfs_disc`).
- `sha1.h` is the tests' SHA-1.

### port/fmv/test

- `fmv_fields_test.c`: with the disc, every `.pss` demuxed whole and its pictures' field flags and
  cadence counted (`fmv_fields`).
- `fmv_test.c`: the player's parts without the game: a synthetic PSS, the demuxer, the decoder
  (`fmv`).
- `rd_video_test.c`: `rd_video.c` on [rd](ARCHITECTURE.md#terms)'s headless device, pictures in and
  the present target read back (`rd_video`, `rd_video_mali`).
- `rd_video_window_test.c`: the renderer's output following the swapchain on SDL's offscreen
  driver when the window is resized (`rd_video_window`).

### port/game/test

- `achievements_test.c`: the game-state view over a synthetic snapshot, every achievement's
  condition, suspension, `achievements.toml` round trips (`achievements`).
- `appearance_disc_test.c`: the characters' colours against the disc's CLUTs (`appearance_disc`).
- `appearance_test.c`: the recolour on synthetic CLUTs: identity, coverage, swatches, randomize
  (`appearance`).
- `credits_test.c`: `staffroll.c` over a short table with the port credit appended, and the
  credits row's lock (`credits`).
- `door_grace_test.c`: the doorway fix's arm and grace and a copy of the exit check's guard
  (`door_grace`).
- `hand_ik_test.c`: the hand records as `ACTGame_ConnectHand` sets them through the real
  `handManager.c`: every target finite, a NaN into `GetTableArcCos` giving 0 and a counted fault
  (`hand_ik`).
- `ico_frame_test.c`: the [native frame](ARCHITECTURE.md#terms)'s widths for each aspect and the
  options' frame (`ico_frame`).
- `kanban_boot_test.c`: the boot's card check and signs (`kanbanBoot.c`) for a first and a later
  boot (`kanban_boot`).
- `model_viewer_cam_test.c`: the viewer's camera step without the game (`model_viewer_cam`).
- `model_viewer_test.c`: the viewer's table against its motion-kind blocks and the game's table
  sizes, its names in five languages (`model_viewer`).
- `model_viewer_headless.cmake` with `model_viewer_pad.txt`: the headless game driven from boot to
  the first model, an animation and back (`model_viewer_headless`).
- `options_test.c`: the gameplay options' defaults and setters, and `eBrainGetTarget` with Yorda
  safe off and on (`options`).
- `photo_test.c`: photo mode's state and both cameras, identity, moves, limits (`photo`).
- `poly_flat_test.c`: `IsPointIsInScreen`'s x bound is the 4:3 half width at every native width
  (`poly_flat`).
- `shadow_spawn_test.c`: the depth a shadow rises from, read at the generator kind's word as the
  PS2 reads it; with the disc, every generator row (`shadow_spawn`).
- `title_logo_test.c`: the logo's models and the hidden state's steps (`title_logo`).

### port/input/test

- `input_record_test.c`: the pad recording's format, round trip through the script reader, the
  change-only rule and the sample point (`input_record`).
- `input_test.c`: bindings from config text, dead zone, quantisation, the stick fix, merging, the
  [libpad](ARCHITECTURE.md#terms) buffer, rumble, Escape (`input`).
- `mouse_camera_test.c`: the mouse camera's speed and range for the hand camera (`mouse_camera`).
- `pad_script_test.c`: the `--pad-script` reader and what the null pad then shows the game's pad
  code (`pad_script`).
- `touch_test.c`: the touch overlay's layouts over a table of screens, the sticks and buttons
  (`touch`).

### port/math/test

- `math_test.c`: the PS2 float helpers bit for bit, the R register, matrices, vectors and
  quaternions against references (`math`).
- `newlib_test.c`: `ico_rand`, `ico_qsort`, libm values in both FP environments (`newlib`).
- `softdouble_test.c`: `softdouble.c` against the EE's own soft float from `sce/libgcc`
  (`softdouble`); `dpbit_harness.cmake` builds that oracle on the host.

### port/platform/test

- `android_paths_test.c`: the Android file layout on any platform (`android_paths`).
- `arena_test.c`: the EE RAM [arena](ARCHITECTURE.md#terms): alignment, zero fill, address mapping,
  heap statistics (`arena`).
- `diag_test.c`: the crash report, assert, watchdog and progress cases, each in a child process
  whose log is read back (`diag`).
- `dump_zip_test.c`: F12's zip and the aspect sweep's zip, written and read back with miniz
  (`dump_zip`).
- `fiber_test.c`: [fibers](ARCHITECTURE.md#terms) switched round-robin with their stacks checked,
  deep stacks, the FP state inside a fiber (`fiber`, `fiber_guard`).
- `gpu_driver_test.c`: the Android driver packages: `meta.json`, refusals, installs from zips made
  here, the trial marker (`gpu_driver`).
- `host_config_test.c`: `ico-pc.ini` parsing and rewriting, SHA-1 vectors, path joining; built
  again as `host_config_portable_test` (`host_config`, `host_config_portable`).
- `host_fs_test.c`: the file helpers on a UTF-8 path outside ASCII (`host_fs`).
- `hotkeys_test.c`: F12, Shift+F12 and F11, repeats ignored, no other key taken (`hotkeys`).
- `ios_chain_test.c`: the game's thread, message and memory layer on the fiber scheduler under
  simulated vsyncs (`ios_chain`).
- `lifecycle_test.c`: the mobile lifecycle's step tables on fake operations (`lifecycle`).
- `memory_test.c`: the game's allocator over the arena, [partitions](ARCHITECTURE.md#terms) at the
  EE's offsets (`memory`).
- `movie_pad_test.c`: the pad as a movie's skip check sees it from inside the player's loop
  (`movie_pad`).
- `pace_loop_test.c`: the [vsync lock](ARCHITECTURE.md#terms)'s loop on a grid of the wall clock
  under a fake clock and a fake swapchain (`pace_loop`).
- `pace_policy_test.c`: the slow-present decision, Resolution "auto"'s steps and the gap between
  presents (`pace_policy`).
- `perf_block_test.c`: a block of replays by phase: sums, maxes, the replays over a threshold, the
  longest phase (`perf_block`).
- `sched_test.c`: the fiber scheduler against the EE kernel's rules through the SDK calls
  (`sched`).
- `steam_shortcut_test.c`: Add to Steam's CRC and id, a sample `shortcuts.vdf` appended byte-true,
  the duplicate check, `loginusers.vdf` (`steam_shortcut`).
- `window_video_test.c`: fullscreen and mode requests on SDL's offscreen driver (`window_video`).
- `port/platform/fpenv_test.c`: the simulation's FP mode rounds toward zero and flushes denormals,
  the host mode is back to nearest; `trap` checks the fptrap build traps (`fpenv`, `fpenv_trap`).

### port/render/test

- `fog_lut_test.c`: the depth fog's game side, the same on every compiler (`fog_lut`;
  `fog_lut_arm64` under qemu).
- `gltf_test.c`: the glTF reader and writer, bit-exact round trips, GLB, a Blender-style file,
  refusals (`gltf`).
- `gsb_cull_test.c`: the game's cull against the blended pictures' cameras and the windows that
  scale with the width; with the disc, a stage (`gsb_cull`, `gsb_cull_stage`).
- `modelpack_test.c`: model packs end to end on synthetic models under the build tree
  (`modelpack`).
- `rd_blur_test.c`: `staticBlur.c`'s full-screen effects on rd (`rd_blur`, sharded).
- `rd_boot_present_test.c`: a saved start-up through the presenter, present by present; needs
  `ICO_BOOT_DUMP_DIR` (`rd_boot_present`).
- `rd_crt_test.c`: the CRT filter's options and its shader (`rd_crt`, `rd_crt_fixed`).
- `rd_debug_test.c`: `debug.c` on the host, recorded (`rd_debug`).
- `rd_filter_test.c`: the model viewer's draw filter on synthetic keys (`rd_filter`).
- `rd_fog_selftest_test.c` with `rd_fog_selftest.cmake`: the fog's self-test and probe; its log line
  matched for each path, MSAA counts, odd widths and [Full pixel](ARCHITECTURE.md#terms)
  (`rd_fog_selftest*`).
- `rd_fog_test.c`: the depth fog of `fog_DrawFog` on the CPU and on the device, every depth path
  (`rd_fog`, `rd_fog_*`).
- `rd_gsbase_test.c`: `GsBase.c`'s host path driving a synthetic scene (`rd_gsbase`).
- `rd_interp_test.c`: presentation between [ticks](ARCHITECTURE.md#terms) on two synthetic frames
  (`rd_interp`).
- `rd_layout_test.c`: the layout frame from the game's layout calls (`rd_layout`).
- `rd_mesh_test.c`: the mesh path from `Packet.c` to the VU shaders (`rd_mesh`, sharded).
- `rd_mip_test.c`: the Enhanced mip chain's size and its builder staying inside it (`rd_mip`).
- `rd_mirror_test.c`: mirror mode's flag and the flipped presenter output (`rd_mirror`).
- `rd_msaa_test.c`: the anti-aliasing's MSAA against the frame without it (`rd_msaa`).
- `rd_native_test.c`: the native frame's centre rule on a synthetic frame at five widths
  (`rd_native`).
- `rd_perf_test.c`: the renderer's CPU cost per replay and its steady state (`rd_perf`,
  `rd_perf_mali`).
- `rd_pixel_test.c`: rd through the Vulkan [RHI](ARCHITECTURE.md#terms), pixel by pixel; leaves the
  dump the tool tests replay (`rd_pixel`).
- `rd_present_test.c`: the display options through `GsBase.c` and the presenter (`rd_present`).
- `rd_raw_test.c`: the raw packet builders outside `seki` (`rd_raw`).
- `rd_replay_tool_list.cmake`: `--list --no-device` on `rd_pixel`'s dump (`rd_replay_tool_list`).
- `rd_replay_tool_pass_dump.cmake`: `--pass-dump`, `--target` and `--aspect` on that dump
  (`rd_replay_tool_pass_dump`).
- `rd_depth_copy_dump.cmake`: a player dump with the fog replayed with the depth in place, copied
  and through a buffer, SCENE byte for byte the same (`rd_fog_depth_copy_dump`, `_d24s8`).
- `rd_shadow_test.c`: the shadow count on the stencil (`rd_shadow`).
- `rd_state_test.c`: the recording half of rd without a GPU: list order, defaults, state leakage
  (`rd_state`).
- `rd_tex_test.c`: the texture cache and `Texture.c` on synthetic TIM2 files (`rd_tex`).
- `rd_water_test.c`: the surfaces that render to and sample from textures (`rd_water`).
- `texpack_disc_test.c`: the PCSX2 names of every texture on the disc, and how many of a pack's
  files they reach (`texpack_disc`).
- `texpack_e2e_test.c`: the texture pack end to end on the real pieces (`texpack_e2e`).
- `texpack_load_test.c`: the DDS and PNG loaders on files built in memory; with a pack, its files
  (`texpack_load`, `texpack_pack_files`).
- `texpack_name_test.c`: XXH3, the swizzle and the names against PCSX2's rules (`texpack_name`).
- `texpack_test.c`: the pack's index and loader on fake files (`texpack`).
- Helpers: `mesh_env.h` (the mesh tests' game stubs), `vu_models.h` (synthetic models),
  `sheet_ref.c`/`.h` (the sheet-text shader on the CPU).

### port/rhi/test

- `rhi_vk_test.c`: the exact-texel RHI checks on Vulkan, a hazard cell, the pipeline cache file
  (`rhi_vk`, `rhi_vk_nodual`, `rhi_vk_d24s8`, `rhi_vk_mali`).
- `rhi_d3d12_test.c`: the same checks on D3D12, a Windows GUI program that writes a log and shows
  a message box (`rhi_d3d12`).
- `rhi_d3d12_plan_test.c`: the D3D12 backend's state and barrier bookkeeping on the CPU
  (`rhi_d3d12_plan`).
- `rhi_vk_enum_test.c`: every `rhi.h` enumeration has a Vulkan mapping (`rhi_vk_enum`).
- `rhi_vk_present_mode_test.c`: the present-mode choice over the sets drivers offer
  (`rhi_vk_present_mode`).
- `rhi_vk_swapchain_test.c`: the window path without a display, on SDL's offscreen driver
  (`rhi_vk_swapchain`).
- Helpers: `rhi_test_common.c`/`.h` (the shared cells), `shaders/` (`rhi_test.hlsl`, its compiled
  headers and `gen_shaders.sh`), and `compare_backends.cmd` with `compare_png.ps1`, which render
  every dump on Vulkan and D3D12 on Windows and compare the PNGs within 1 LSB.

### port/shaders/test

- `shaders_table_test.c`: every embedded entry compiled for SPIR-V and signed DXIL
  (`shaders_table`).
- `gs_math_test.c`: `gs_math.hlsli` compiled as C against the [GS](ARCHITECTURE.md#terms) formulas
  (`gs_math`); `hlsl_shim.h` makes it compile.
- `shaders_pixel_test.c`: sprite, blit and blend shaders through the Vulkan RHI
  (`shaders_pixel`, `_nodual`, `_mali`).
- `vu1_test.c`: the [VU1](ARCHITECTURE.md#terms) program shaders against their CPU references and
  hand traces (`vu1`, `vu1_mali`).
- `port/test/gs_blend_test.c` is a standalone CPU program on the GS blender's drift, not a ctest.

### port/ui/test

- `credits_headless.py`: Extras > Credits in the headless game (`credits_headless`).
- `font_coverage_test.c`: every character of the five languages has a glyph (`font_coverage`);
  `font_corpus/` holds the per-language text.
- `font_edge_test.c`: the port's plain text at the output's resolution on a device (`font_edge`).
- `gallery_sweep.py`: every stream and a sample of every bank in one headless run
  (`gallery_sweep`).
- `gallery_test.c`: the music gallery's list from the base ELF's tables (`gallery`).
- `menu_look_test.c`: the menus' Arimo text against the game's menu sheets from the disc
  (`menu_look`); `menu_sheets.c`/`.h` decode the sheets.
- `menu_text_test.c`: the game's menu words and the port's rows drawn in the sheets' look
  (`menu_text`).
- `settings_test.c`, `settings_extra_test.c`, `settings_extras_test.c`: the Settings menu over
  fake tables, CPU only (`settings`, `settings_extra`, `settings_extras`).
- `settings_photo_test.c`: the Settings screens through rd on a device, written as PNGs
  (`settings_render`).
- `ui_mouse_test.c`: the pointer's arithmetic in the menus (`ui_mouse`).
- `ui_test.c`: the embedded font, UTF-8, the runtime text (`ui`).
- Helpers: `settings_fixture.c`/`.h`, `headless_common.py`.

## Render gates

The frame gates replay saved [.rddump](ARCHITECTURE.md#terms) files with the replay tool. Dumps
hold game assets and are never committed, so the dump lists name paths on one machine; a missing
file is reported and skipped, and every gate exits 77 when it has nothing to run.

`tools/native_frame_check.py` has four commands (`tools/native_frame_check.py`, docstring):

- `baseline` renders every dump of the list at the gate settings (`original_4x3_1x` and
  `enhanced_4x3_2x`), every named target and the presented picture, and writes the SHA-256 of
  each target's pixels to `docs/native_frame_baseline.json` with the harness commit
  (`cmd_baseline`).
- `gate-a` renders again and compares with the manifest exactly, reporting dump, settings, target
  and, with `--ref-dir`, the first differing pixel; exit 1 on any mismatch (ctest
  `native_frame_gate_a`).
- `gate-b` takes one game state captured at 4:3, 16:9, 32:9, 48:9 and 60:9 (an aspect sweep: a
  zip, a folder or PNGs with the aspect in the name, or `ICO_SWEEPS`) and checks the centre
  columns of every aspect equal the 4:3 picture byte for byte, with `--effects R` allowing a band
  at each old edge (ctest `native_frame_gate_b`).
- `selftest` checks the gate-b logic on synthetic pictures (ctest `native_frame_check_selftest`).

The list is `tools/native_frame_dumps.txt`, or the list or folder `ICO_DUMPS` names; the ctests
skip unless `ICO_DUMPS` is set. The baseline is updated by running `baseline` after a change whose
pictures are accepted and committing the new manifest.

`tools/coverage_check.py` (ctest `coverage_corpus`, `coverage_check_selftest`) renders every dump
of `tools/coverage_corpus.txt` at its own aspect, `--enhanced --present 1376x896`, once with
`--aa off` and once with `--aa msaa4`, plus the interpolated frame of a dump that names a `PREV`,
and compares with the previous run: any mesh drawn fewer times than before fails, and more than
`pixels` percent of pixels changed by more than `tol` fails (defaults 0.5 and 8). The previous
run's pictures and counts live in `tools/.coverage/` (ignored by git; `ICO_COVERAGE_DIR`); a
passing item rolls its baseline forward and a failing one keeps the old, so the next run still
reports it. `seed` overwrites every baseline after a build that is accepted
(`tools/coverage_check.py`, docstring). Both tools hold a shared replay lock,
`build-host/tmp/replay.lock` or `ICO_REPLAY_LOCK`, so two runs never compete.

The script tests under `port/render/test/` run the tool or a test program and check its output:
`rd_replay_tool_list.cmake` and `rd_replay_tool_pass_dump.cmake` on the dump `rd_pixel` leaves
behind, `rd_depth_copy_dump.cmake` on a player dump with the depth fog (`ICO_FOG_DUMP`), and
`rd_fog_selftest.cmake`, which matches the `fog: depth path` line of the self-test
(`port/render/CMakeLists.txt`). The ctest `rd_replay_tool` renders `rd_pixel`'s dump with
`--present 640x480`; it requires `rd_pixel` to have run first (`FIXTURES_REQUIRED rd_dump`).

## Tests that need the disc

Three cache variables point the tests at the player's files ([../BUILDING.md](../BUILDING.md#tests)
has their defaults):

- `ICO_DISC_IMAGE`, the PAL image: `vfs_disc`, `archive_disc`, `sndn2`, `fmv`, `fmv_fields`,
  `appearance_disc`, `shadow_spawn`, `gsb_cull_stage`, `texpack_disc`, `menu_look`, `menu_text`,
  `gallery`, `tables_loader`, and the headless game runs below. Each exits 77 without it.
- `ICO_BASE_ELF`, the PAL base ELF: with pyelftools, `tables_loader` and `tables_manifest` are
  built from it (`port/data/CMakeLists.txt`); `gallery` reads its tables.
- `ICO_TEXPACK_DIR`, a PCSX2 pack's replacements folder: `texpack_disc`, `texpack_e2e`,
  `texpack_pack_files`.

Three tests start the headless game itself, each in a fresh folder with its own `ico-pc.ini` and a
pad script, `RUN_SERIAL` and with a long timeout:
`model_viewer_headless` (`port/game/test/model_viewer_headless.cmake` with
`model_viewer_pad.txt`), `gallery_sweep` (`port/ui/test/gallery_sweep.py`, which plays through
`ICO_GALLERY_PLAY`) and `credits_headless` (`port/ui/test/credits_headless.py`, with
`unlock_credits=1`); the two scripts share `headless_common.py`, whose pad walks the boot to the
title and then Settings > Extras. A worktree configured with `-DICO_DISC_IMAGE=/nonexistent
-DICO_BASE_ELF=/nonexistent` cannot start the game in any of them (`tools/dev_build.sh`, header);
the ctest exclusion used for such runs, `-E
"^(model_viewer_headless|gallery_sweep|credits_headless|rd_replay_tool)$"`, also leaves out
`rd_replay_tool`, whose dump comes from `rd_pixel`.

## Environment switches

The variants that force GPU paths a desktop driver does not take are in
[../BUILDING.md](../BUILDING.md#tests)'s table (`ICO_RD_NO_DUAL`, `ICO_VK_FAKE_NO_DUAL`,
`ICO_VK_FAKE_D24S8`, `ICO_VK_FAKE_LIMITS=mali|min`, `ICO_RD_BLEND=storage|fixed`). The other
switches the tests and their registrations use:

| variable | read in | effect |
| --- | --- | --- |
| `ICO_VK_FAKE_NO_DST`, `ICO_DX_FAKE_NO_DST` | `vk_device.c`, `d3d12_device.c` | a device that cannot read the pixel it draws over |
| `ICO_VK_FAKE_NO_MSAA` | `vk_device.c` | single sampling only |
| `ICO_VK_VALIDATION` | `vk_device.c` | the validation layer; the render and RHI tests set it with `VK_ADD_LAYER_PATH` when the layer is in the toolchain |
| `ICO_VK_GLOBAL_BARRIERS` | `vk_cmd.c` | a global barrier before every pass instead of hazard tracking, for A/B runs |
| `ICO_RD_FOG_PATH` | `rd_fog_path.c` | `inplace`, `copy` or `buffer` forces the fog's depth path; `test` makes the self-test try every path |
| `ICO_RD_DEPTH_COPY` | `rd_fog_path.c` | 0 samples the depth in place, 1 copies it |
| `ICO_RD_FOG_SABOTAGE` | `rd_fog_path.c` | makes named paths fail the self-test (`rd_fog_selftest_fallback`) |
| `ICO_RHI_BACKEND`, `ICO_VK_DEVICE`, `ICO_D3D12_ADAPTER` | `rhi_backend.c`, `vk_device.c`, `d3d12_device.c` | pick the backend, the Vulkan device, the D3D12 adapter (`warp`) |
| `ICO_FIXED_CLOCK` | `clock.c` | the wall clock fixed at 2002-01-01 unless `0` |
| `ICO_GALLERY_PLAY` | `gallery.c` | a play script for the music gallery |
| `ICO_UI_POPUP_TEST` | `ui_host.c` | the test popup at [Main tick](ARCHITECTURE.md#terms) 100 and every 150 ticks after (`[dev] popup_test`) |
| `ICO_START_STAGE` | `ico2/common/src/debug.c` (`debug_TryToGetStartStage`), `gamestate.c` | `[dev] start_stage` handed over: a stage 2 to 105 to start in, which suspends achievements |
| `ICO_TTY` | `kernel_host.c` | the game's `scePrintf` to stdout |
| `ICO_FMV_DECODE` | `movie.c` | 0 skips decoding in the [headless build](ARCHITECTURE.md#terms) |
| `ICO_RD_DUMP_EVERY`, `_DIR`, `_INTERP`, `_FROM` | `rd_core.c` | the frame dumps `ico-pc.ini` hands to rd |
| `ICO_VU_REF_AT` | `rd_replay_tool.c` | the kicked triangles containing a GS point |
| `ICO_BOOT_DUMPS`, `ICO_BOOT_OLD` | `rd_boot_present_test.c` | the start-up dumps and their kind |
| `ICO_DUMPS`, `ICO_SWEEPS`, `ICO_COVERAGE_DIR`, `ICO_REPLAY_LOCK` | the gate tools | the dump list, the aspect sweeps, the coverage state, the lock |

`grep -rn 'getenv("' port/` and `grep -a -rn 'getenv("' ico2/` list the rest (the game sources
read `ICO_START_STAGE`, `ICO_SWITCH_TO`, `ICO_SWITCH_AT` and `ICO_HEAP_ASAN_FREE`).

## F12 dumps and the replay tool

In the [window build](ARCHITECTURE.md#terms) F12 writes an `.rddump` of the last closed frame, a PNG
of the whole DISPLAY frame and a PNG of the picture as [presented](ARCHITECTURE.md#terms) into the
[user folder](ARCHITECTURE.md#terms)'s `dumps/`, then puts the dump and a copy of the log into
`frame-<time>-v<vsync>.zip` and deletes the loose dump; the zip exists because issue trackers refuse
`.rddump` attachments (`port/platform/hotkeys.h`, `ico_frame_dump_paths`, `ico_frame_present_path`;
`port/platform/dump_zip.h`, `ico_frame_dump_zip`). F11 makes the stats lines come every second for
30 s.

Shift+F12 in photo mode is the aspect sweep: the paused frame re-rendered at 4:3, 16:9, 32:9, 48:9
and 60:9 into `sweep-<time>.zip`, holding `<aspect>.rddump` and `<aspect>.png` for each with the
4:3 member first, so the replay tool, which takes a zip's first `.rddump`, replays it
(`port/platform/dump_zip.h`, `ico_sweep_zip`, `ico_sweep_aspect_name`). The aspect tokens are the
ones `native_frame_check.py gate-b` reads.
`[dev] dump_every = N` writes every Nth frame ([PLATFORM.md](PLATFORM.md#hotkeys-and-dump-zips)).

The dump format is `port/render/rd_dump.c`'s header comment, and the replay tool's options are
listed in [RENDER.md](RENDER.md#dumps-and-the-replay-tool). For tests the useful ones are
`--pass-dump DIR` (every named target from one run), `--list --no-device` (the commands without a
GPU), `--present WxH`, `--aspect`, `--enhanced`, `--aa` and `--interp T PREV`
(`port/render/tools/rd_replay_tool.c`, usage).

## Pad scripts, recordings and the trace

A pad script drives the pad instead of a person, in the [headless build](ARCHITECTURE.md#terms)
through `pad_script=` in `ico-pc.ini`, `--pad-script FILE` or a `pad-script.txt` beside the program
(`port/platform/main_host.c`, header comment). Each line is `<tick> <buttons-hex> [lx ly rx ry]`:
from that [Main tick](ARCHITECTURE.md#terms) on, the pad returns the line's values until the next
line; ticks increase strictly; the buttons are the game's logical word, active high (`0x0040` Cross,
`0x0010` Triangle, `0x0800` Start, `0x1000` up, `0x4000` down, `0x8000` left, `0x2000` right); the
sticks are 0 to 255 with 128 centred (`port/input/pad_script.h`, header comment). The pad read of
tick t sees the line for tick t, and `--ticks N` stops after ticks 0 to N-1, so scripts, traces and
the tick limit share one numbering.

A pad recording writes what the game read from the pad as a pad script, so the headless build
replays a player's session: the sample is taken in `scePadRead`, keyed by the tick that read it,
one line per tick whose value changed, closed by `# end at tick T: N lines, C ticks read twice
with different values` (`port/input/input_record.h`, header comment). It is on by default in the
window build, in `logs/input-<time>.txt`, and off headless (`input_record=`); its header records
the values of `gameplay.stick_fix`, `yorda_safe`, `developer_mode`, `door_fix`, `dev.start_stage`
and `video.anti_aliasing`, and the native frame's width and aspect (`port/platform/main_host.c`,
`record_keys`, `record_header`; `ico_input_record_frame_lines`).

The trace (`trace=` or `--trace FILE`) writes a header naming its columns and then one line per Main
tick after the vsync in which the tick finished: `tick vsync stage sys0 sys1 gameover gf0 .. gf12
save`, the story flags as thirteen 8-digit hex words and `save` the FNV-1a 32-bit hash of the
checkpoint save image (`port/platform/trace_host.h`, header comment). A run is a function of its
input and the vsync count
([ARCHITECTURE.md](ARCHITECTURE.md#ee-kernel-threads-semaphores-interrupts)), so the same build,
script and disc give the same trace.

## The developer build helper

`tools/dev_build.sh <worktree-or-.> <target>... [-- <ctest args>]` configures
`<worktree>/build-host/linux-x64` once with the `linux-x64` preset and `-DICO_LINK_EXE=ON` (and
`-DICO_DISC_IMAGE=/nonexistent -DICO_BASE_ELF=/nonexistent` in a worktree without `baserom/`),
builds exactly the named targets at `-j4` under a shared build lock
(`build-host/tmp/build.lock`, `ICO_BUILD_LOCK`), then runs ctest with the arguments after `--`,
failing when they select no test, and prints a one-line summary (`tools/dev_build.sh`, header).

## The audits as tests

The audits over the game sources are tools, some run as ctests on native builds (`offset_audit`,
`template_audit`, `const_write_audit`, `softdouble_gate`) and the rest in the pre-commit hook and
CI; what each refuses is in [GAME_CODE.md](GAME_CODE.md#audits). Other script tests under
`port/test/CMakeLists.txt` and the top level check generated files and tables (`layout_asserts`,
`layout_asserts_fresh`, `data_desc_fresh`, `font_audit`, `content_table_chains`) and the package
staging (`package_textures`, [PACKS.md](PACKS.md#tests)).

## CI

| workflow | job | runner | what it runs |
| --- | --- | --- | --- |
| `ci.yml` | `linux` | `ubuntu-24.04` | the hygiene checks (no disc data, format, no `ICO_HOST` conditional, stack-layout, struct-cast and call-type audits, generated files fresh), then `linux-x64` headless and window builds with `ctest`, `linux-x64-clang` with `ctest`, and the `win-x64` cross build (tests built, not run) |
| `ci.yml` | `extra` | `ubuntu-24.04` | a matrix of `asan` and `fptrap` (build `all tests`, `ctest`) and the `win-x64-clang` cross build |
| `ci.yml` | `linux-arm64` | `ubuntu-24.04-arm` | the no-disc-data check, the `linux-arm64` preset with `ctest`, the filtered-pixel render tests in a step that does not gate, the arm64 package on release tags |
| `ci.yml` | `android` | `ubuntu-24.04` | the debug APK (`assembleDebug`), `tools/check_android_flags.sh`, the APK structure checks |
| `ios.yml` | `build`, `publish` | `macos-15`, `ubuntu-24.04` | the unsigned iPhone IPA for a published release, attached to it |
| `ios.yml` | `build-macos`, `publish-macos` | `macos-15`, `ubuntu-24.04` | the macOS app for a published release, attached to it |

The triggers, steps and caches are [../BUILDING.md](../BUILDING.md#continuous-integration)'s.
