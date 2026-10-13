# Game data: the disc layer

How the port reads the player's disc: the VFS and its two backends (the disc image read in place,
and the extracted archive `ico.o2r`), the first-run extractor that writes the archive, the
[libcdvd](ARCHITECTURE.md#terms) and [SIF](ARCHITECTURE.md#terms) host layers under the game's own
cdvd manager, the IOP RAM stand-in, the loader that fills the game's data tables from the disc's
boot ELF, and the port's own reads of DATA.DF. The repository holds no disc data
([LEGAL.md](../LEGAL.md)): what is here is structure, sector numbers and hashes.

## Files

| path | what it holds |
| --- | --- |
| `port/data/vfs.h`, `vfs.c` | the VFS: the backend table, path spellings, byte reads over sector reads, the disc slot |
| `port/data/iso9660.c` | the ISO9660 backend and `IcoDiscImage`, the logical bytes of an `.iso`, `.chd`, `.bin` or `.cue` |
| `port/data/archive.h`, `archive.c` | `ico.o2r`: its layout, the disc's verification values, the archive backend |
| `port/data/extract.h`, `extract.c` | the first-run extractor and the compiled DATA.DF manifest |
| `port/data/cdvd_host.h`, `cdvd_host.c` | the `sceCd*` functions the game calls, over the VFS disc |
| `port/data/sif_host.h`, `sif_host.c` | `sceSif*` with no IOP: the IOP heap, SIF DMA, RPC to host servers |
| `port/data/sifdev_host.c` | `sceOpen` and the other sifdev file calls, for the development kit's `host0:` device |
| `port/data/iop_ram.h`, `iop_ram.c` | `ico_iop_ram`, the 2 MB of IOP RAM, and the IOP heap allocator |
| `port/data/tables.h`, `tables.c` | the runtime loader of the 73 data tables |
| `port/data/gen/` | the generated table definitions, descriptors and address registry (`table_defs.c`, `table_desc.c`, `table_desc.h`, `ee_symbols.c`, `tables.cmake`) |
| `port/data/df_pack.h`, `df_pack.c` | reads of DATA.DF's loose files and pack members for the port's own screens |
| `port/data/json.h`, `json.c` | the JSON reader (`ico_json`) |
| `port/data/chdr_miniz_names.h` | prefixes the second miniz copy inside libchdr |
| `port/data/CMakeLists.txt` | the libraries (`ico_port_data`, `ico_miniz`, `ico_libchdr`, `ico_json`) and the tests |
| `port/platform/android/iso_import.h`, `iso_import.c` | the Android first start's copy of the chosen image |
| `ico2/fumi/ios/cdvd.c`, `ico2/seki/src/FileManager.c` | the game's side: the cdvd manager and the boot loader |
| `config/tables_manifest.txt` | the CRC-32 of every table row and the addresses the tables' pointers hold |

## Layers

```
game  fumi/ios/cdvd.c          the cdvd manager thread, its request queue, the background
                               request table, the stream manager, the directory cache,
                               DATA.DF's directory (unifile_read_func)
      seki/src/FileManager.c   boot: file_Init, file_LoadCDFile
      fumi/sound/adpcm_init.c  ADPCM streams into IOP memory
---------------------------- the SDK's function names (port/compat/libcdvd.h, sifrpc.h, sifdev.h)
port  port/data/cdvd_host.c    sceCd* over the VFS disc
      port/data/sif_host.c     sceSif*: IOP heap, SIF DMA, RPC to host servers
      port/data/iop_ram.c      ico_iop_ram and the IOP heap
      port/data/vfs.c          paths, byte reads, the disc slot
      port/data/iso9660.c      backend: the disc image in place
      port/data/archive.c      backend: ico.o2r
      port/data/extract.c      writes ico.o2r on the first run
      port/data/tables.c       the data tables, from the boot ELF on the disc
```

The seam is the SDK's own function names: the game calls `sceCdRead`, and the host build links
`port/data`'s definition. Everything above it in `cdvd.c` (the queue, the threads, the cache, the
retry and drive-recovery paths) runs as written (`port/data/cdvd_host.c`, header comment).

## The VFS

The game addresses the disc as libcdvd does: it looks a file up by its ISO9660 path, gets its first
logical sector number (LSN) and byte size, and reads whole 2048-byte sectors by LSN. It also does
LSN arithmetic of its own: DATA.DF's directory gives byte offsets inside DATA.DF, and
`unifile_read_func` (`ico2/fumi/ios/cdvd.c`) records each member as DATA.DF's LSN + offset / 2048.
So every backend serves one flat LSN space with the disc's own numbering (`port/data/vfs.h`).

| call | what |
| --- | --- |
| `ico_vfs_mount(backend, location)`, `ico_vfs_unmount` | open a volume through a backend table; the reason for a failure goes to stderr |
| `ico_vfs_set_disc`, `ico_vfs_disc` | the volume the libcdvd layer reads (not owned; `NULL` is "no disc") |
| `ico_vfs_normalize` | any accepted spelling to `DIR/FILE` |
| `ico_vfs_stat`, `ico_vfs_open`, `ico_vfs_size` | a look-up: LSN, size, ISO date, directory flag, name as the volume spells it |
| `ico_vfs_read(file, offset, dst, len)` | bytes of a file, short at its end |
| `ico_vfs_read_sectors(vfs, lsn, count, dst)` | whole sectors anywhere on the volume |
| `ico_vfs_lsn_to_offset`, `ico_vfs_offset_to_lsn`, `ico_vfs_size_to_sectors` | sector arithmetic (`ICO_VFS_SECTOR`, 2048) |

A backend (`IcoVfsBackend`) has `mount`, `unmount`, `lookup` (a normalized path), `read_sectors`
(-1 when any sector lies outside the volume) and `volume_sectors`. Paths are case-insensitive, and
`ico_vfs_normalize` accepts `DFDATAS/DATA.DF`, `DFDATAS\DATA.DF;1`, `\DFDATAS\DATA.DF;1`,
`cdrom0:\SYSTEM.CNF;1` and `/SCES_507.60`: it strips a `cdrom0:` or `cdrom:` prefix, leading
separators and the `;<version>` suffix, maps `\` to `/` and upper-cases.

## Disc images

`ico_vfs_iso9660` (`port/data/iso9660.c`) reads an image in place. It reads the primary volume
descriptor from LSN 16 and walks directories from the root record; it needs no path table and no
Joliet or Rock Ridge, and refuses a multi-extent entry. A directory's recorded length may be the
bytes in use rather than whole sectors (PS2 mastering tools write it so): the walk reads every
sector the length touches and stops at a zero length byte in each.

Every container gives the same logical bytes, the ISO's, through `IcoDiscImage`
(`ico_disc_image_open`, `ico_disc_image_read`, `ico_disc_image_bytes`):

| image | how it is read |
| --- | --- |
| `.iso` | as it is |
| `.chd`, DVD (`chdman createdvd`) | 2048-byte units in order through libchdr, one hunk cached |
| `.chd`, CD (`chdman createcd`) | 2448-byte frames (2352 sector bytes and 96 subcode bytes) described by the `CHT2` or `CHTR` track metadata; the first track must be data, and each frame's 2048 user bytes are taken where the track type puts them (`cd_data_offset`) |
| `.bin` | 2352-byte raw sectors, recognised by the 12-byte sync pattern at the start (`raw_sync`); byte 15 is the mode: mode 1 data starts at byte 16, mode 2 form 1 at byte 24; a form 2 sector is refused |
| `.cue` | parsed by `ico_cue_parse` for its first `FILE`, first `TRACK` type and `INDEX 01`; the `.bin` it names is opened from beside the sheet, and the sheet's type and the sector header must agree. A sheet with several files, or whose first track is audio, is refused |

A CHD that needs a parent image is refused. libchdr is compiled from the source tree
`tools/fetch_deps.sh` installs, with its bundled LZMA, miniz, zstd and FLAC decoders, so the program
needs no system zlib or zstd; `chdr_miniz_names.h`, forced into every libchdr file, gives its miniz
copy a `chdr_` prefix beside the port's own `ico_miniz` (`port/data/CMakeLists.txt`).

## The archive: ico.o2r

`ico.o2r` (`port/data/archive.h`) is a ZIP whose entries are stored, not compressed:

| entry | holds |
| --- | --- |
| `disc/<PATH>` | the disc file `PATH` (`DFDATAS/DATA.DF`, `SCES_507.60`), byte for byte |
| `tail/<PATH>` | the bytes after the file's end up to its last sector's end, only when they are not all zero |
| `meta.json` | the source image (SHA-1, size, the rule that accepted it), the extractor, the disc id, the volume's sector count, and for every disc file and directory the archive answers for: path, ISO name, first LSN, size and date |

`ico_vfs_archive` answers look-ups from `meta.json` and reads sectors by LSN: each stored file
covers the LSNs it had on the disc, so DATA.DF keeps one contiguous range and the game's LSN
arithmetic addresses the same bytes as on the disc. A sector no stored file covers reads as an
error. At mount, miniz reads the ZIP's central directory, `meta.json` is parsed (`port/data/json.h`)
and each entry's data offset is found from its local header; after that a sector read is a plain
`fseek` and `fread` (`port/data/archive.c`).

`ico_archive_read_info` opens an archive and checks its structure (every entry `meta.json` names is
stored, uncompressed, inside the file, at the size `meta.json` gives). `ico_archive_info_acceptable`
accepts it only for format version `ICO_ARCHIVE_VERSION` (1), the extractor string
`ICO_ARCHIVE_EXTRACTOR`, the disc id `SCES-50760`, the rule `iso-sha1` (with the image SHA-1
`ICO_DISC_ISO_SHA1`) or `elf-sha1+datadf-crc`, and the boot ELF SHA-1 `ICO_DISC_ELF_SHA1`.
`mount_archive` in `port/platform/main_host.c` then reads `SCES_507.60` back through the mounted
archive and checks its SHA-1 again before making it the disc; the log line is `ico_pc: game data
<path> (SCES-50760, N files, N bytes; from an image with SHA-1 ..., accepted by <rule>)`.

## The first run

`mount_game_data` (`port/platform/main_host.c`) looks for `ico.o2r` in the per-user folder
(`ico_host_pref_dir`: SDL's pref path in the window build, the executable's folder in the headless
build, the portable folder in portable mode), then beside the executable. The first usable one is
mounted; an unusable one is logged (`ico_pc: <path> is not usable: <why>`) and skipped. With none,
the first run finds the disc image, extracts it into the per-user folder, and mounts the result.

`find_iso` (`main_host.c`) looks for the image in this order: `--iso`; `Ico_PAL.iso`, `Ico_PAL.chd`,
`Ico_PAL.bin`, `Ico_PAL.cue` beside the executable (`image_names`); `iso=` in `ico-pc.ini`; the
`ICO_ISO` environment variable; the same four names under `baserom/` in the working folder; then a
file-open dialog (native on Windows, SDL3's in the other window builds), whose answer is saved as
`iso=` once it is verified. On iOS the executable's folder is the app's Documents folder
(`ico_host_exe_dir`, `port/platform/host_config.c`), and with no image there the run ends with a
message box instead of a dialog. On Android the image comes from the system's file picker
(`find_iso_android`, below).

`ico_extract_archive` (`port/data/extract.h`) mounts the image with the ISO9660 backend and stores
`SYSTEM.CNF`, the boot file its `BOOT2` line names, `SCES_507.60`, every `*.IRX` module in the root
(the port reads `SNDN2DRV.IRX`'s pitch table and `LIBSD.IRX`'s reverb presets and idle voice
block), and every file under `DFDATAS/`, DATA.DF whole. It hashes the whole image, streams each file
into the ZIP with no compression (hashing `SCES_507.60` and taking the CRC-32 of DATA.DF's
directory and members on the way), and writes `meta.json` last, once the image is accepted. The
rules, in order:

| rule | accepts when |
| --- | --- |
| `iso-sha1` | the image's logical SHA-1 is `ICO_DISC_ISO_SHA1` (for a `.chd` or `.bin`, the SHA-1 of the ISO it holds) |
| `elf-sha1+datadf-crc` | `SCES_507.60` hashes to `ICO_DISC_ELF_SHA1`, and DATA.DF has the PAL disc's size, directory and members: the CRC-32 of its directory and of each of its 193 members match the manifest compiled into `extract.c` (a re-dump with other padding or volume size passes) |

Anything else is refused and nothing is left behind; `IcoExtractResult` says whether the file was
`unreadable` (not an image this program reads) or the `wrong_disc` (another region, edition or a
modified image), which `mount_game_data` turns into the message box the player sees.
`ICO_EXTRACT_NO_VERIFY` (tests on a synthetic image) writes the rule `unverified`, which
`ico_archive_info_acceptable` refuses. The archive is written to `<out>.tmp` and moved over `<out>`
once it is complete and reads back (`MoveFileEx` on Windows); a failure or a cancel removes the
`.tmp`. Progress runs through `IcoExtractProgressFn` in the phases `open`, `hash`, `extract`,
`finish`; the window build shows it in a small window and the log.

The DATA.DF manifest in `extract.c` (between the `BEGIN DATA.DF MANIFEST` and `END` lines) holds
DATA.DF's size, the directory's member count and CRC-32, and each member's offset, size and CRC-32:
no names and no bytes of the file. `archive_test manifest <iso>` prints it again
(`ico_extract_print_datadf_manifest`).

Android has no file beside the program: `find_iso_android` (`main_host.c`) asks the system's file
picker, and `ico_iso_import` (`port/platform/android/iso_import.h`) copies the chosen `content://`
stream into the app's files folder as `Ico_PAL.iso`, `.chd` or `.bin` (the kind from the header,
`ico_iso_ext_for`; a `.cue` sheet is refused, `ico_iso_is_cue`), after checking that the image and
`ICO_ISO_IMPORT_SPARE_BYTES` (1.2 GB for the extracted data and a margin) are free. The extractor
then reads the copy like any file, and `main_host.c` deletes it once the game data is mounted,
unless `keep_image=1` is in `ico-pc.ini`. A cancelled or refused copy is removed.

`use_iso=1` in `ico-pc.ini` (or `[dev] use_iso`) skips the archive and mounts the image directly
with `ico_cdvd_host_mount_iso`; it is the headless build's default, so trace runs and tests read the
image (`use_iso_mode`). In that mode the image's SHA-1 is checked unless `verify=0` or
`--no-verify` (`verify_iso`); the extractor always verifies.

## libcdvd on the host

`port/data/cdvd_host.c` defines the `sceCd*` subset that `ico2/fumi/ios/cdvd.c` and
`ico2/seki/src/FileManager.c` call (`port/compat/libcdvd.h` declares them) over `ico_vfs_disc()`:
`sceCdInit`, `sceCdMmode`, `sceCdDiskReady`, `sceCdStatus`, `sceCdGetDiskType`, `sceCdGetError`,
`sceCdBreak`, `sceCdSearchFile`, `sceCdRead`, `sceCdReadIOPm`, `sceCdSync`, `sceCdSyncS`,
`sceCdReadClock` and the stream calls `sceCdSt*` and `sceCdStream`.

- `sceCdSearchFile` fills libcdvd's 0x24-byte record (LSN, size, name, the date in libcdvd's
  order). `sceCdGetDiskType` reports a PS2 DVD only when the disc holds the boot file `SYSTEM.CNF`
  names (`ico_cdvd_host_boot_name`). The drive never reports a tray-open or not-ready state while a
  disc is mounted.
- `sceCdRead` copies the sectors at once; `sceCdReadIOPm` copies them into `ico_iop_ram` at the IOP
  address it is given, and logs `cdvd: sceCdReadIOPm to 0x... is outside IOP RAM` for an address
  outside it.
- Completion follows the drive's timing at [vsync](ARCHITECTURE.md#terms) grain: the buffer is
  full when the call returns (no caller can see it before syncing), but a non-blocking
  `sceCdSync` (odd mode) reports the command busy until the next simulated vsync, and a blocking
  one (even mode) from a game thread blocks that thread until that vsync, as libcdvd's
  `sceCdDelayThread` loop does, so the other threads, the same-priority Main among them, run
  while the drive works. `ico_cdvd_host_vsync` completes the command; `sceCdInit` registers it with
  the host loop (`port/platform/host_loop.h`) when that exists, and tests call it themselves. From
  the host context the wait completes at once.
- The stream calls read the requested sectors straight from the disc, so the ring is always full
  and `sceCdStStat` reports its size; pause and resume change nothing.
- `sceCdReadClock` reports the port clock (`ico_clock_now`, `port/platform/clock.c`: local time,
  or a fixed moment under `ICO_FIXED_CLOCK`) in BCD; a test can set its own source with
  `ico_cdvd_host_set_clock_source`.
- With no disc in the slot, `sceCdInit` mounts `ico_cdvd_host_default_iso()`: `ICO_ISO`, else
  `baserom/Ico_PAL.iso` (tests and developer runs). `ico_cdvd_host_stats` feeds the diagnostics
  heartbeat.

The game's side keeps its own code with a few host lines in `ico2/fumi/ios/cdvd.c`: the stream
thread's record is sized for the host's wider `IOSThread`, the stream ring pointer and the request
record's address are pointer-wide (`ICO_WORD`). `file_Init` in `ico2/seki/src/FileManager.c` skips
the `IOPRP224.IMG` reboot and the IRX module loads (there is no IOP) but keeps `file_WaitDisc`, so
a missing disc stops boot there as an empty drive did.

## SIF and IOP RAM

There is no [IOP](ARCHITECTURE.md#terms). `port/data/sif_host.c` answers the EE side of the SIF
(`port/compat/sifrpc.h`, `sifdev.h`):

- The IOP reboot and module loads (`sceSifRebootIop`, `sceSifResetIop`, `sceSifLoadModule`, ...)
  report success; `sceSifLoadModule` hands out increasing module ids.
- `sceSifAllocIopHeap` and `sceSifFreeIopHeap` use `ico_iop_heap_alloc` and `ico_iop_heap_free`
  (`port/data/iop_ram.h`): first fit, 16-byte rounding, between `ICO_IOP_HEAP_BASE` (0x10000; below
  it stands for the IOP kernel and modules) and the end of the 2 MB `ico_iop_ram`. Address 0 is
  never handed out, so the game's "0 means the allocation failed" tests hold.
- `sceSifSetDma` copies each record's bytes from the EE address to `ico_iop_ram` at once and
  returns a nonzero id; `sceSifDmaStat` always reports the transfer finished; a record outside IOP
  RAM fails the call (0).
- An RPC server is a host function registered with `ico_sif_register_server(sid, fn)` (eight at
  most). `sceSifBindRpc` answers with a token for a registered server, `sceSifCallRpc` runs the
  server synchronously and copies its reply into the receive buffer (zeros for a `NULL` reply), and
  `sceSifCheckStatRpc` always reports the call done. The one server is the sound driver,
  `ico_sndn2_host_serve` (`port/audio/sndn2_host.c`). `ico_sif_host_last_rpc` feeds the heartbeat.

IOP addresses are the game's: an `int` from `sceSifAllocIopHeap` handed to `sceCdReadIOPm`,
`sceSifSetDma`, `SgStAdpcmOpen` and the sound driver. `ico_iop_ptr` turns one into a host pointer
(after `ico_iop_phys` strips the kseg bits) and `ico_iop_addr` turns a pointer back.

`port/data/sifdev_host.c` gives the sifdev file calls (`sceOpen`, `sceClose`, `sceRead`,
`sceWrite`, `sceLseek`) the development kit's `host0:` device, which the development build's tools
used (way data, camera sets, particle files, stage settings, `debug.c`'s option table and
snapshots): `host0:object/stagesetting/st04a.ssb` is `<pref>/dev/object/stagesetting/st04a.ssb`.
Backslashes become slashes, a leading `./`, `/` or `\` and a `;1` suffix are dropped, a `..`
component is refused, and `SCE_CREAT` makes missing folders. Any other device fails with -1.
Descriptors are the lowest free number from 0, as the kit handed them out.

## The data tables

The game's 73 data tables are defined empty on the host (`port/data/gen/table_defs.c`) and filled
before the game starts by `load_tables` (`main_host.c`), which calls `ico_tables_load_vfs`
(`port/data/tables.h`): it reads `SCES_507.60` from the mounted disc and decodes every record field
by field into the host layout that `port/data/gen/table_desc.c` describes (`IcoTableRow`,
`IcoTableRecord`, `IcoTableField`):

| field kind | how it is filled |
| --- | --- |
| `ICO_TF_INT`, `ICO_TF_FLOAT`, `ICO_TF_BYTES` | copied (the EE is little-endian, as every host the port builds for; `tables.c` refuses to compile otherwise) |
| `ICO_TF_BITS` | extracted from its unit and stored through the field's setter |
| `ICO_TF_FUNC` | the host function the address registry (`port/data/gen/ee_symbols.c`) names at that EE address |
| `ICO_TF_DATA` | the member's own string pool (`staffroll_dat`), else the registry's host object |

`ico_tables_load_elf` finds the `.data`, `.rodata` and `.sdata` sections, checks every row's range
and its CRC-32 against `config/tables_manifest.txt` before writing anything, copies the string pools
first, then decodes the records. An address the registry does not know fails the load with the
table, index, field and both addresses (`tables: <member> <symbol>[i].<field>: no function is known
at 0x...`); `main_host.c` logs it and ends the run with `Cannot read the game's data from <source>`.
A good load logs `ico_pc: N data table rows (N records) loaded from SCES_507.60`.
[GAME_CODE.md](GAME_CODE.md#data-tables) covers the generator and the maintainer's reference test.

## DATA.DF for the port's own screens

DATA.DF (`DFDATAS/DATA.DF`) starts with a directory: a count, then 40-byte entries of a 32-byte
name, a byte offset and a byte size. The entries named `*.DF` are packs: a raw deflate stream whose
output is a 16-byte header (the member count first), 0x224-byte member entries (id, kind, a word,
size, name) and the members back to back (`iosCdvdMgrPackLoad` in `ico2/fumi/ios/cdvd.c`). The
other entries are loose files (`.int` streams, `.smb`, `.pss`, `.jim`).

`port/data/df_pack.h` reads these outside the game's cdvd thread, for the music gallery
(`port/ui/gallery_play.c`) and the disc tests: `ico_df_has`, `ico_df_size` and `ico_df_read` for a
loose file; `ico_df_find_member` and `ico_df_read_member` for a pack member by its full name
(`sound/ICO_SE/com_v.hd`), first match in directory order. The members' index is built once per
volume, on the first look-up, by inflating each pack's header and directory; a member is read by
inflating its pack from the start to the member's end.

## JSON

`port/data/json.h` is a small RFC 8259 reader (library `ico_json`, no dependencies) for the port's
own files: the archive's `meta.json`, glTF model files (`port/render/gltf.c`) and a GPU driver
package's `meta.json` (`port/platform/gpu_driver.c`). `ico_json_parse` copies the text, so the
caller may free it at once; every node lives until `ico_json_free`, which is safe after a failed
parse and idempotent. The accessors (`ico_json_get`, `ico_json_at`, `ico_json_count`,
`ico_json_u64`, ...) accept `NULL` and return `NULL`, 0 or -1 for it, so look-ups chain without
checks. The grammar is strict: one value with optional whitespace and a leading UTF-8 byte order
mark, no comments, trailing commas, leading zeros, `NaN` or control characters, nesting at most
`ICO_JSON_DEPTH_MAX` deep; duplicate keys are kept and `ico_json_get` returns the first.

## Tests

| test | checks |
| --- | --- |
| `vfs_synthetic`, `vfs_disc` | the VFS and ISO9660 backend on an image the test builds, and on the player's disc (`ICO_DISC_IMAGE`; skipped, 77, without it) |
| `archive_synthetic`, `archive_disc` | extraction into a temporary archive and its mount, compared with the ISO backend |
| `iso_import` | the Android copy on Linux with SDL's memory streams: byte for byte, cancel, write failure, the space arithmetic (Linux window build) |
| `json` | numbers, nesting and the depth limit, escapes, malformed input |
| `data_desc_fresh` | `port/data/gen/` is what `tools/gen_data_desc.py` writes |
| `tables_loader`, `tables_manifest` | the loader against reference tables generated from the base ELF, and the manifest against the ELF (built only with `ICO_BASE_ELF` and pyelftools) |

All are registered in `port/data/CMakeLists.txt`.
