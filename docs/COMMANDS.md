# Command-line reference

DBPFX comes with two ready-made command-line clients:

| Client | What it is | Where to find it |
|---|---|---|
| [`dbpfx-cli`](#dbpfx-cli) | The full tool, built on the C++ engine | `artifacts\native\dbpfx-cli.exe` (Windows), `artifacts/native/dbpfx-cli` (Linux) |
| [`DBPFX.Inspect`](#dbpfxinspect) | A small example built on the .NET library | `managed/samples/DBPFX.Inspect/bin/Release/net10.0/` |

Two development tools are covered at the end: [`dbpfx-bench`](#dbpfx-bench) and [`dbpfx-iconstamp`](#dbpfx-iconstamp).

> [!NOTE]
> Every client checks the operating system first. Below the [minimum](../README.md#requirements) it stops with an error. On a supported system older than the recommended one, it prints a `note:` line and carries on.

## Contents

- [Before you start](#before-you-start)
- [dbpfx-cli](#dbpfx-cli)
  - [version](#version) · [info](#info) · [list](#list) · [extract](#extract) · [stbl](#stbl) · [verify](#verify) · [repack](#repack) · [types](#types)
- [DBPFX.Inspect](#dbpfxinspect)
  - [info](#info-1) · [strings](#strings) · [convert](#convert)
- [Recipes](#recipes)
- [Exit codes](#exit-codes)
- [Development tools](#development-tools)

---

## Before you start

### Running the tool

Use a full path, or open a terminal in the folder that holds the tool.

**PowerShell** (Windows)
```powershell
$cli = "C:\path\to\DBPFX\artifacts\native\dbpfx-cli.exe"
& $cli info "C:\Games\Mods\MyMod.package"
```

**Command Prompt** (Windows)
```bat
set CLI=C:\path\to\DBPFX\artifacts\native\dbpfx-cli.exe
"%CLI%" info "C:\Games\Mods\MyMod.package"
```

**Terminal** (Linux)
```bash
cli=~/DBPFX/artifacts/native/dbpfx-cli
"$cli" info ~/mods/MyMod.package
```

> [!TIP]
> Variables only last as long as the window they were set in. In a new window, set `$cli` (or `CLI`) again.

### Resource keys

A resource is identified by its **type**, **group** and **instance**, written in hexadecimal:

```
TTTTTTTT:GGGGGGGG:IIIIIIIIIIIIIIII
220557DA:80000000:00F7A2C3F2E55B21
```

`-` works as a separator too (`220557DA-80000000-00F7A2C3F2E55B21`). The `list` command prints every key in a package.

### Games

| Name on the command line | Game | Package format |
|---|---|---|
| `sims3` / `Sims3` | The Sims 3 | DBPF 2.0 |
| `medieval` / `Medieval` | The Sims Medieval | DBPF 2.0 |
| `sims4` / `Sims4` | The Sims 4 | DBPF 2.1 |

`dbpfx-cli` takes the lowercase names; `DBPFX.Inspect` accepts any capitalisation.

---

## dbpfx-cli

```
dbpfx-cli <command> <package> [options]
dbpfx-cli version
```

| Command | Purpose |
|---|---|
| [`version`](#version) | Engine version, and whether this system is supported |
| [`info`](#info) | Header and summary of a package |
| [`list`](#list) | Every resource, optionally only one type |
| [`extract`](#extract) | Save one resource to a file, decompressed |
| [`stbl`](#stbl) | Print the text of the string tables |
| [`verify`](#verify) | Decompress every resource and report damaged ones |
| [`repack`](#repack) | Rewrite a package, convert it to another game, or change compression |
| [`types`](#types) | Count resource types across one or many packages and find unknown ones |

### version

```
dbpfx-cli version
dbpfx-cli --version
```

```
DBPFX 0.1.0-canary
platform: Windows 10.0 build 26100 (supported, recommended)
```

The second line reads `supported, older than recommended` on Windows 10, glibc 2.35–2.38 or macOS 14, and `NOT supported` below the minimum.

### info

```
dbpfx-cli info <package>
```

The package's format, the game it belongs to, its index and how much data it holds.

```powershell
& $cli info "C:\Program Files (x86)\Steam\steamapps\common\The Sims 3\GameData\Shared\Packages\FullBuild0.package"
& $cli info MyMod.package
```

```
DBPF 2.0  (The Sims 3)
user version  0.0
entries       6
index         @0x0000383F, 196 bytes, flags 0x0
compressed    3, deleted records 0
data          14303 bytes stored, 38870 bytes uncompressed
```

| Line | Meaning |
|---|---|
| `DBPF 2.0` / `DBPF 2.1` | Format version. 2.0 is The Sims 3 or Medieval, 2.1 is The Sims 4 |
| `entries` | Number of resources in the index, deleted records included |
| `compressed` | Resources stored compressed (RefPack or zlib) |
| `deleted records` | The Sims 4 entries that mark a resource as removed |
| `data` | Total size as stored in the file, and after decompression |

### list

```
dbpfx-cli list <package> [type]
```

One line per resource. Give a type ID in hexadecimal, with or without `0x`, to see only that type.

```powershell
& $cli list MyMod.package
& $cli list MyMod.package 220557DA       # string tables only
& $cli list MyMod.package 0x00B2D882     # DDS images only
```

```
     0  220557DA:80000000:00F7A2C3F2E55B21  off=0x00000060  file=       59  mem=       60  zlib     String table
     1  545AC67A:80000000:00F7A2C3F2E55B22  off=0x0000009B  file=     2960  mem=    19333  zlib     SimData
     2  62E94D38:80000000:00F7A2C3F2E55B23  off=0x00000C2B  file=     1673  mem=     5145  refpack  Combined binary tuning
     3  00B2D882:80000000:00F7A2C3F2E55B24  off=0x000012B4  file=     2000  mem=     2000  none     DST image
     4  545AC67A:80000000:00F7A2C3F2E55B99  off=0x00001A84  file=        1  mem=4294967295  deleted  SimData
```

| Column | Meaning |
|---|---|
| 1 | Position in the index |
| 2 | Resource key |
| `off` | Where the data starts in the file |
| `file` | Bytes as stored |
| `mem` | Bytes after decompression |
| 6 | Storage: `none`, `refpack`, `zlib`, `stream` or `deleted` |
| 7 | Type name for this package's game, or `(unknown type)` |

Deleted records (The Sims 4) have no data; their `mem` column shows `4294967295`.

To save the list, redirect it to a file:

```powershell
& $cli list MyMod.package > MyMod-contents.txt
```

### extract

```
dbpfx-cli extract <package> <key> <output file>
```

Saves one resource, already decompressed, to a file.

```powershell
& $cli extract MyMod.package 220557DA:80000000:00F7A2C3F2E55B21 strings.stbl
& $cli extract MyMod.package 00B2D882:80000000:00F7A2C3F2E55B24 texture.dst
```

```
2000 bytes (zero-copy)
```

`(zero-copy)` means the resource was stored uncompressed and went straight from the file to the output. If the key is not in the package, the tool prints `not found` and exits with code 1.

### stbl

```
dbpfx-cli stbl <package> [max per table]
```

Prints the text of every string table (`220557DA`) in the package, 20 lines per table unless you give another number.

```powershell
& $cli stbl MyMod.package
& $cli stbl MyMod.package 5          # first 5 strings of each table
& $cli stbl MyMod.package 1000000    # everything
```

The Sims 3:
```
== 220557DA:00000000:0017A2C3F2E55B21
  0xE52A9C1EC7E4B6D1  Plumbob
  0x00000000DEADBEEF  Ñandú – ünïcødé ✓
```

The Sims 4:
```
== 220557DA:80000000:00F7A2C3F2E55B21
  0x2A9C1EC7  Plumbob
  0xDEADBEEF  Ñandú ✓
```

The Sims 3 tables have 64-bit keys, The Sims 4 tables 32-bit keys. Text is printed as UTF-8; in an older Windows console run `chcp 65001` first if accented characters look wrong.

### verify

```
dbpfx-cli verify <package>
```

Reads and decompresses every resource. Prints the damaged ones and a summary.

```powershell
& $cli verify FullBuild0.package
```

```
102127 resources, 0 failed
```

Exits with code **0** when everything decodes and **1** when anything fails, so it works in scripts:

```powershell
& $cli verify MyMod.package
if ($LASTEXITCODE -ne 0) { Write-Warning "MyMod.package is damaged" }
```

### repack

```
dbpfx-cli repack <package> <output> [game] [compression]
```

Writes a new package. Untouched resources are copied byte for byte. The source file is never changed.

| Option | Values | Default |
|---|---|---|
| `game` | `sims3`, `medieval`, `sims4` | The game of the source package |
| `compression` | `keep`, `always`, `never` | `keep` |

| Compression | Effect |
|---|---|
| `keep` | Each resource keeps its current storage |
| `always` | Compress every resource that gets smaller |
| `never` | Store everything uncompressed |

```powershell
# Rewrite the package as it is (also rebuilds a messy index)
& $cli repack MyMod.package MyMod-clean.package

# Convert a The Sims 4 package to The Sims 3 format
& $cli repack TS4Mod.package TS3Mod.package sims3

# Same game, but compress everything
& $cli repack MyMod.package MyMod-small.package sims4 always

# Store everything uncompressed (easier to inspect in a hex editor)
& $cli repack MyMod.package MyMod-raw.package sims3 never
```

```
wrote 81 resources
```

To choose a compression mode you must also name the game. Any other text in the game position counts as `sims3`.

Converting to The Sims 3 or Medieval re-encodes zlib data as RefPack and drops The Sims 4 deleted records. This changes the **container** only: game-specific resource formats inside it are not translated.

> [!IMPORTANT]
> Conversion from The Sims 4 to The Sims 3 has been tested on real files. `medieval` as a target uses the same format as The Sims 3 but has not been tested on real packages yet.

### types

```
dbpfx-cli types <package> [more packages ...]
```

Counts every resource type over one or many packages. Types the built-in table does not know are listed at the end, each with a sample and a guess at its content.

```powershell
& $cli types MyMod.package
& $cli types FullBuild0.package FullBuild1.package FullBuild2.package
```

```
game      type           count          bytes  name
TS3/TSM   0333406C          2           9724  XML resource  _XML
TS3/TSM   00B2D882          1           3000  DDS image  _IMG
TS3/TSM   0166038C          1             50  Name map  NMAP
TS3/TSM   073FAA07          1          25995  Script assembly  S3SA
TS3/TSM   220557DA          1            101  String table  STBL
TS4       00B2D882          1           2000  DST image
TS4       220557DA          1             60  String table
TS4       545AC67A          1          19333  SimData
TS4       62E94D38          1           5145  Combined binary tuning

2 package(s), 9 resource types, 0 unknown
```

Rows are grouped by game, because the same type ID can mean different things in The Sims 3 and The Sims 4. The last column is the type's name and, where it has one, its short tag.

When unknown types turn up:

```
unknown types - largest sample of each:

TS4 12345678  3 resources, 120..4096 bytes each
  sample   12345678:00000000:0000000000ABCDEF in C:\Mods\Something.package
  content  binary, FourCC tags: GEOM,VRTF
  bytes    47 45 4F 4D 0C 00 00 00 ... |GEOM....|
```

Neither Command Prompt nor PowerShell expands `*.package` for the program. To scan a whole folder:

**PowerShell**
```powershell
& $cli types (Get-ChildItem "C:\Games\The Sims 3\GameData\Shared\Packages" -Filter *.package).FullName

# a folder and all its subfolders
& $cli types (Get-ChildItem "$env:USERPROFILE\Documents\Electronic Arts\The Sims 4\Mods" -Recurse -Filter *.package).FullName
```

**Linux**
```bash
"$cli" types ~/mods/*.package
find ~/mods -name '*.package' -exec "$cli" types {} +
```

A package that cannot be opened is reported on its own line and the scan continues.

---

## DBPFX.Inspect

A short example program for the .NET library, useful as a starting point for your own tools.

```
DBPFX.Inspect info    <package>
DBPFX.Inspect strings <package>
DBPFX.Inspect convert <package> <output> <Sims3|Medieval|Sims4>
```

Run the built program directly, or through `dotnet run` from the source folder:

```powershell
$inspect = "C:\path\to\DBPFX\managed\samples\DBPFX.Inspect\bin\Release\net10.0\DBPFX.Inspect.exe"
& $inspect info MyMod.package

# or, from the repository root
dotnet run --project managed\samples\DBPFX.Inspect -c Release -- info MyMod.package
```

`DBPFX.Native.dll` (or `libDBPFX.Native.so` on Linux) must be next to the program. The build puts it there.

### info

```
DBPFX.Inspect info <package>
```

The package, its game and the engine version, then the resource types it holds, most common first.

```
C:\Mods\MyMod.package - DBPF 2.1, 5 entries - Sims4 - DBPFX 0.1.0-canary
  545AC67A        2  SimData
  220557DA        1  String table
  62E94D38        1  Combined binary tuning
  00B2D882        1  DST image
```

### strings

```
DBPFX.Inspect strings <package>
```

Every string table with its format (`Legacy` for The Sims 3, `Modern` for The Sims 4) and size, then all of its strings.

```
220557DA:80000000:00F7A2C3F2E55B21  Modern, 3 strings
  000000002A9C1EC7  Plumbob
  00000000DEADBEEF  Ñandú ✓
```

### convert

```
DBPFX.Inspect convert <package> <output> <Sims3|Medieval|Sims4>
```

Same conversion as `dbpfx-cli repack` with a game, through the .NET API.

```powershell
& $inspect convert TS4Mod.package TS3Mod.package Sims3
```

```
TS3Mod.package written for Sims3
```

---

## Recipes

**Check that every package in a folder is intact** (PowerShell)
```powershell
Get-ChildItem "C:\Games\Mods" -Recurse -Filter *.package | ForEach-Object {
    & $cli verify $_.FullName | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Warning "damaged: $($_.FullName)" }
}
```

**Find which packages contain a resource** (PowerShell)
```powershell
$key = "220557DA:80000000:00F7A2C3F2E55B21"
Get-ChildItem "C:\Games\Mods" -Recurse -Filter *.package |
    Where-Object { (& $cli list $_.FullName) -match $key } |
    Select-Object -ExpandProperty FullName
```

**Extract every string table of a package** (PowerShell)
```powershell
& $cli list MyMod.package 220557DA | ForEach-Object {
    $key = ($_ -split '\s+')[2]
    & $cli extract MyMod.package $key ("{0}.stbl" -f ($key -replace ':', '_'))
}
```

**Convert a folder of The Sims 4 packages for The Sims 3** (PowerShell)
```powershell
New-Item -ItemType Directory -Force converted | Out-Null
Get-ChildItem *.package | ForEach-Object {
    & $cli repack $_.FullName (Join-Path converted $_.Name) sims3
}
```

**Same tasks on Linux**
```bash
for f in ~/mods/*.package; do "$cli" verify "$f" > /dev/null || echo "damaged: $f"; done
for f in ~/mods/*.package; do "$cli" list "$f" | grep -q 220557DA:80000000:00F7A2C3F2E55B21 && echo "$f"; done
mkdir -p converted && for f in *.package; do "$cli" repack "$f" "converted/$f" sims3; done
```

---

## Exit codes

| Code | `dbpfx-cli` | `DBPFX.Inspect` |
|---|---|---|
| `0` | Success | Success |
| `1` | An error (file missing, damaged package, unsupported system, resource not found), or `verify` found damaged resources | An unhandled error (.NET exception) |
| `2` | Missing arguments or unknown command | Missing arguments or unknown command |

Errors go to standard error as `error: <message>`, so redirecting the normal output (`> file.txt`) still shows them in the terminal.

---

## Development tools

These are built with the native engine but are meant for development, not everyday use.

### dbpfx-bench

```
dbpfx-bench <package> [lookups]
```

Times opening a package, looking resources up by key (2000 by default) and reading every resource, against a plain file-reading baseline.

```powershell
& "C:\path\to\DBPFX\build\native\dbpfx-bench.exe" FullBuild0.package 10000
```

### dbpfx-iconstamp

```
dbpfx-iconstamp <DBPFX.Identity.dll> <STABLE|BETA|CANARY|DEVELOPER> <file.exe> [more.exe ...]
```

Windows only. Copies one icon group out of `DBPFX.Identity.dll` into executables, replacing their existing icons. The build uses it to give every DBPFX executable the icon of the current release channel. Do not use it on signed executables: changing their resources breaks the signature.
