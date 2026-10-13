# Saves: memory cards

How the port keeps the game's saves: [libmc](ARCHITECTURE.md#terms) answered from a folder on the
host in the layout PCSX2's folder memory cards use, a second card in port 1 when one is configured,
where those folders are on each platform, and the importer that copies an existing PS2 save into
a card folder. The game's own card code (`ico2/fumi/ios/mcard.c`, `mcdata.c`) runs unchanged.

## Files

| path | what it holds |
| --- | --- |
| `port/save/mc_host.c` | libmc over a host folder: the `sceMc*` functions the game calls |
| `port/save/mc_host.h` | the card folders of the two ports, the pending-request query, the card's size |
| `port/compat/libmc.h` | the host's libmc header: the directory entry, the result codes, the function numbers, the calls |
| `port/save/mc_import.h`, `mc_import.c` | the importer: a raw card image or a `.psu` archive into a card folder |
| `tools/mc_import.c` | `mc_import`, the command-line importer |
| `port/platform/host_config.c` | `ico_host_saves_dir`, `ico_host_saves2_dir`: where the card folders are |
| `port/save/CMakeLists.txt` | `ico_save` (the card), `ico_save_import` (the importer), the `mc_import` tool, the `save` test |
| `port/save/test/save_test.c` | the `save` test |
| `ico2/fumi/ios/mcard.c`, `mcdata.c` | the game's side: the card manager thread, the save and load steps, the icon files |

## The card

Port 0 always holds a formatted 8 MB card whose root is the card folder; port 1 holds a second card
only when a folder is configured for it, and is empty otherwise, as on a PS2 with one card
(`port/save/mc_host.c`, header comment, `slot_ok`). Slot 0 is the only slot. `sceMcGetInfo`
reports a PS2 card (type 2), formatted, with its free space in 1 KB clusters: 8000 less the
clusters the folder's tree takes, each file rounded up to whole clusters and each directory one
cluster (`port/save/mc_host.c`, `sceMcGetInfo`, `used_clusters`; `port/save/mc_host.h`,
`ICO_MC_HOST_CLUSTERS`). The first `sceMcGetInfo` after `sceMcInit` answers
`sceMcResChangedCard` once, a card freshly inserted, as the game's card check expects
(`port/save/mc_host.c`, `sceMcInit`).

The files are stored byte for byte as the card holds them, in PCSX2's folder-card layout:
`<card>/BESCES-50760ico/` holding `icon.sys`, `boy_blk.ico`, the product file `BESCES-50760ico`
and the save files `game.000` onward, the names the game itself uses (`port/save/mc_host.c`,
header comment; `ico2/fumi/ios/mcard.c`, `iOSMcSaveSeg`). Nothing else is written, and nothing at
all before the game's first `sceMcMkdir`: until then a card folder that does not exist is an
empty card, and `sceMcMkdir` creates the card folder and any missing parent
(`port/save/mc_host.c`, `sceMcGetDir`, `sceMcMkdir`, `make_root`). The PC-only facts about a
save, the mirror-mode flag of each slot and the achievements' run state, live outside the card,
in `config.toml`'s `[mirror]` and `achievements.toml`, keyed by the slot and the save block's
checksum ([CONFIG.md](CONFIG.md); `port/game/options.h`, `ico_mirror_slot_saved`;
`port/game/achievements.h`, `ico_ach_slot_saved`).

Names follow the card's rules: at most 31 bytes, none of `\ : * ? " < > |` or a control
character, at most 8 directories deep; `.` and `..` resolve against each port's current directory
(`port/save/mc_host.c`, `valid_name`, `resolve`). A subdirectory's `sceMcGetDir` lists `.` and
`..` first, as a card does, and the entries are sorted by name; a pattern takes `*` and `?`
(`port/save/mc_host.c`, `sceMcGetDir`, `list_dir`, `wild`). Dates in the directory entries are
the host file's modification time in UTC (`port/save/mc_host.c`, `fill_entry`, `fill_date`).

On Linux and on Android's app folder, where the file system tells case apart, a path component
that does not exist as spelled takes the name of the one entry in its directory that differs only
in ASCII case, so a card folder copied in as `besces-50760ICO` is still the game's; Windows and
macOS ignore case themselves (`port/save/mc_host.c`, `fold_case`).

`sceMcFormat` never wipes the folder, which may hold other games' saves; `sceMcUnformat` only
marks the port unformatted until the next `sceMcFormat` (`port/save/mc_host.c`, `sceMcFormat`,
`sceMcUnformat`).

## The calls

`port/compat/libmc.h` declares the calls the game makes and nothing else: `sceMcInit`,
`sceMcSync`, `sceMcGetInfo`, `sceMcFormat`, `sceMcUnformat`, `sceMcMkdir`, `sceMcChdir`,
`sceMcGetDir`, `sceMcDelete`, `sceMcOpen`, `sceMcClose`, `sceMcSeek`, `sceMcRead`, `sceMcWrite`
and `sceMcFlush`, with the 64-byte `sceMcTblGetDir` entry, the `sceMcRes*` result codes the game
compares against and the `sceMcFuncNo*` numbers `sceMcSync` reports
(`port/compat/libmc.h`, header comment). The header is written from this project's clean-room
`sce/libmc/libmc.h`, with pointer-sized types made host-correct.

The result codes the port returns, and when:

| code | value | returned for |
| --- | --- | --- |
| `sceMcResSucceed` | 0 | success (a read or write returns the byte count, an open the handle) |
| `sceMcResChangedCard` | -1 | the first `sceMcGetInfo` after `sceMcInit` |
| `sceMcResNoFormat` | -2 | a port after `sceMcUnformat` |
| `sceMcResFullDevice` | -3 | a directory or file that cannot be made, a short write, a failed close |
| `sceMcResNoEntry` | -4 | a missing file or directory; `sceMcMkdir` of one that exists |
| `sceMcResDeniedPermit` | -5 | a bad handle, or an open mode that forbids the call |
| `sceMcResNotEmpty` | -6 | `sceMcDelete` of a directory that has entries |
| `sceMcResUpLimitHandle` | -7 | a fourth open file (libmc's limit is three) |
| `sceMcResFailDetect`, `sceMcResFailDetect2` | -9, -10 | a call on an empty port (`-10` from `sceMcGetInfo`) |

(`port/compat/libmc.h`, the `sceMcRes*` enum; `port/save/mc_host.c`, the `begin` call of each
function, `MC_HANDLES`, `unusable`.)

A file opened for writing is written to a hidden copy, `<dir>/.<name>.tmp`, which `sceMcClose`
syncs to the disk and moves over the file; a crash, a power cut or a full disk during a save leaves
the previous file whole. A new file is on the card, empty, from the open, as on the PS2
(`port/save/mc_host.c`, `McHandle`, `close_handle`, `copy_to`, `sceMcOpen`). Opening or writing
a save file (`game.*`) logs one `mc: reading ...` or `mc: writing ...` line, so a log that stops
there says so (`port/save/mc_host.c`, `game_file`, `sceMcOpen`).

## Completion at the next vsync

As the disc's calls do ([DATA.md](DATA.md)), a card request does all its work when it is issued and
completes at the next simulated [vsync](ARCHITECTURE.md#terms): an on-vsync callback marks it done,
and from then `sceMcSync(1, ...)` returns 1 with the function number and the result; before that
it returns 0, and with no request it returns -1. `sceMcSync(0, ...)` waits for the request, which
is done at once (`port/save/mc_host.c`, header comment, `begin`, `vsync`, `sceMcSync`).

The same callback signals the game's card lock semaphore, `IosMcLock`, when it exists: the game's
`iosMcMgrSync` waits on it between its `sceMcSync` polls, so each poll happens once per vsync, the
rate it has on the PS2 (`port/save/mc_host.c`, `vsync`; `ico2/fumi/ios/mcard.c`, `IosMcLock`).
`ico_mc_host_pending` reports whether a request is waiting for its `sceMcSync`
(`port/save/mc_host.h`). The callback is registered by the first `sceMcInit`
(`ico_host_on_vsync_register`; [PLATFORM.md](PLATFORM.md) has the host loop).

## Where the card folders are

The first `sceMcInit` reads the folders unless a caller set them first with
`ico_mc_host_set_root` or `ico_mc_host_set_port_root` (the tests do); setting either port stops
`sceMcInit` reading the configuration for both (`port/save/mc_host.h`;
`port/save/mc_host.c`, `sceMcInit`, `ico_mc_host_set_port_root`).

- Port 0: `saves=` in `ico-pc.ini`, else `[paths] saves` in `config.toml`, a relative path taken
  from the executable's folder; with neither, `memcard/` in the
  [user folder](ARCHITECTURE.md#terms) (`port/platform/host_config.c`, `ico_host_saves_dir`,
  `card_dir_key`). A path too long for the buffer falls back to `memcard` in the working folder,
  with a log line (`port/save/mc_host.c`, `sceMcInit`).
- Port 1: `saves2=` in `ico-pc.ini`, else `[paths] saves2`, relative paths likewise; absent or
  empty, port 1 has no card. With two cards the log says `mc: two cards: port 0 ..., port 1 ...`
  (`port/platform/host_config.c`, `ico_host_saves2_dir`; `port/save/mc_host.c`, `sceMcInit`).

Both functions are a plain read of `ico-pc.ini` over `config.toml` (`ico_ini_load_layered`) with
no environment variable set and no folder made, so they can run on the game's
[fiber](ARCHITECTURE.md#terms) inside `sceMcInit` (`port/platform/host_config.c`, the comment
above `card_dir_key`). They read the `ico-pc.ini` in the executable's folder, and
`ico_ini_load_layered` adds the `config.toml` keys only when that path is the ini layer's own
(`ico_host_ini_path`); in the macOS app bundle, whose ini layer is in the user folder, neither the
user folder's `ico-pc.ini` nor `[paths] saves` is consulted, and the card is `memcard/` in the user
folder unless the bundle's own folder holds an `ico-pc.ini` naming one
(`port/platform/host_config.c`, `ico_ini_load_layered`, `ico_host_ini_path`).

The default folder per platform, `<user folder>/memcard/`:

| platform | default card folder |
| --- | --- |
| Windows | `%APPDATA%\ico-pc\ico-pc\memcard\` (SDL's pref path) |
| Linux | `~/.local/share/ico-pc/ico-pc/memcard/` (SDL's pref path) |
| macOS | `memcard/` in SDL's pref path for `ico-pc` |
| portable mode | `userdata/memcard/` beside the executable |
| Android | `<files>/memcard/`, the app's files folder |
| iOS | `memcard/` in the app's Documents folder |
| [headless build](ARCHITECTURE.md#terms), tests | `memcard/` beside the executable |

(`port/platform/host_config.c`, `ico_host_pref_dir`, `ico_host_saves_dir`;
`tools/package_docs_lib.sh`, `pkg_write_ini`, for the Windows and Linux folders;
`port/platform/android/android_paths.h`.) [PLATFORM.md](PLATFORM.md) has the user folder and
portable mode, [CONFIG.md](CONFIG.md) the keys.

## Importing a PS2 save

`ico_mc_import` copies the game's save directory, `BESCES-50760ico`, out of a PS2 memory card
image or a save archive into a card folder (`port/save/mc_import.h`, `ico_mc_import`). The
format is told by the file's contents, not its name (`port/save/mc_import.c`, `detect`):

| format | recognised by | read |
| --- | --- | --- |
| raw card image (`.ps2`, `.bin`) | the superblock's `Sony PS2 Memory Card Format ` at page 0 | yes |
| `.psu` (uLaunchELF, mymc) | a directory entry, then `.` | yes |
| `.max` (Action Replay MAX) | `Ps2PowerSave` | refused, with a message |
| `.cbs` (CodeBreaker) | `CFU` | refused, with a message |

A raw image is read through the card's own file system: the superblock, the FAT reached through
the superblock's indirect FAT cluster list, and directories of 512-byte entries. Pages are 512
bytes on the 8 MB card, followed by 16 bytes of ECC in images that keep them (PCSX2's and mymc's
`.ps2`) and by nothing in plain dumps; the file's size tells which, and the ECC is not checked.
A `.psu` is the save directory's entry, `.` and `..`, then each file's entry followed by its data
padded to 1024 bytes (`port/save/mc_import.c`, header comment, `read_raw`, `read_psu`). Images up
to 80 MB and archives up to 16 MB are read (`IMAGE_MAX`, `ARCHIVE_MAX`).

Only the game's directory is imported; every other entry is counted and the first 32 are named in
the result (`IcoMcImport.skipped`, `skippedName`). Every file is read before any is written, an
existing file of the same name is refused unless `ICO_MC_IMPORT_OVERWRITE` is passed, and each
file is written to a dot-file copy that is synced and moved over its name, as the card writes
(`port/save/mc_import.c`, `write_files`). The result is 0 imported, 1 when the source holds no
ICO save or an empty one, -1 on an error with the reason in `IcoMcImport.why`
(`port/save/mc_import.h`, `ico_mc_import`).

The command-line tool runs it on each file it is given:

```
mc_import [--overwrite] --to SAVES_FOLDER FILE...
```

`SAVES_FOLDER` is the card folder (`[paths] saves` or `saves2`); each file prints its format and
either the files imported or the reason, then the skipped entries. The exit status is 0 when every
file gave a save, 1 when one held none, 2 on an error (`tools/mc_import.c`, `main`, `usage`). The
tool is built by `port/save/CMakeLists.txt` and kept in every build (`cmake/IcoTests.cmake`,
`ICO_KEEP_IN_ALL`). The program itself does not call the importer.

## Tests

`port/save/test/save_test.c` is the ctest `save`. It runs on temporary card folders under the
working directory, on simulated vsyncs, and needs no game data (`port/save/test/save_test.c`,
header comment; `port/save/CMakeLists.txt`):

- `test_libmc`: the `sceMc*` calls the game's card code issues, scripted by hand: the first boot on
  a card with no save, the first save to slot 1, a reload, a re-save that leaves the file
  byte-identical, a delete, and the result codes the game compares.
- `test_mcard`: `ico2/fumi/ios/mcard.c` and `mcdata.c` themselves, compiled unchanged with the
  game's options on the scheduler, driven through the `iosMc*` entry points as `kanbanBoot.c` and
  `layout_action.c` drive them; the bytes left in the card folder are checked against a
  hand-laid-out expectation. A stand-in for the game's background disc read supplies the icon
  files.
- `test_import`: the importer on synthetic card images (plain and with ECC spares) and a synthetic
  `.psu`, the game's files read back through the folder card byte for byte, other games' entries
  skipped and named; the `mc_import` tool (its path is the test's argument) on the same files; and
  the second card in port 1.
