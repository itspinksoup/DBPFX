# DBPFX
A modern implementation of the DBPF Package libs from **The Sims™ 3**, **The Sims™ Medieval** and **The Sims™ 4**.
It reads and writes the packages of all three games and converts resource data between them. It has been produced
with artificial intelligence asistance from multiple LLMs like *Claude Cowork* and *Codex* by *OpenAI*.

This project isn't affiliated with/endorsed by The Sims™, Maxis or Electronic Arts™. Trademarks are property of their respective owners.

### How this was born
DBPFX wasn't meant to replace any of the current existing modding tools, but to bring a more actual and reliable alternative 
for modders and players who wanted to modify or simply manage their games' files. Since in some situations where performance 
with file operations wasn't the expected, it took the challenge to create a new system that could be improved over the time without 
depending on third parties like the s3pi libraries, created Peter L Jones and its contributors, as well as s4pi by Kuree and its contributors.

## Disclaimer
Not everyone wants to use tools that are powered by, or partially or entirely developed with, artificial intelligence. This isn't 
an intend to engage in demagoguery; nevertheless, it's perfectly reasonable for any individual whether a developer or regular user
who might use these libraries to decide for themselves whether they prefer a more experimental alternative or a more traditional one.

As previously stated, parts of the code have been supervised by *Claude Cowork* and *Codex* by *OpenAI*, which has improved on several 
ways how the syntax and logic was from the original interpretation. However, not a single line of it has been written using "vibe coding"
practices. It can be unstable, dangerous, and extremely complex to maintain if one is not familiar with its methodology and the required 
programming language understanding.

There's always room for improvement, so there has been occasions where the AI assistance helped organizing handles, names and numerous
files through the libraries. Best way to check if something has been generated is to check if its syntax enough understandable (as the eyes 
from who supervises this project *cough cough*). 

You can make formal requests to change so it will be addressed and fixed on newer builds. Or you can pull a request by yourself.

## How it works
Conceptually wise it's heavily based on the s3pi & s4pi architectures, but this approach is made in a very different way.
it's a memory-mapped, zero-copy C++20 core driven by a declarative binary grammar and a .NET API (`net10.0` and `netstandard2.1`) 
on top for the C# bindings:

* The OS pages bytes in on demand, so opening a 2 GB package costs the same as opening a 2 KB one.
* Opening reads the 96-byte header. The index layout (which of type, group or instance-high
  are shared by every entry) is compiled once into byte offsets. The whole index is decoded by one
  native call into a struct array the first time .NET asks for it, and key lookups use a hash table
  built on first use.
* Uncompressed resources are spans into the mapped file. A `ResourceLease` holds a reference to the mapping, so its data stays valid
  even after the `PackageFile` is disposed.
* Compressed resources are decoded into pooled buffers (`ArrayPool<byte>`).
* Untouched resources go from the map to the output byte-for-byte. They are *never* decompressed or re-encoded.

It was measured on a generated 60,000-resource list from a 123 MB package file (`native/tools/bench.cpp`) against
the conventional readers like **s3pi** and **s4pi**:

| | DBPFX | s3pi/s4pi |
|---|---|---|
| Open | 0.03 ms | 7.4 ms |
| 2,000 key lookups | 7–13 ms | ~290 ms |
| Read all 306 MB | ~385 ms | ~440 ms |

Reading was limited by `zlib` in both cases.
  
## Documentation
Usage information and resources are slightly being added to this repository. This section will be provided with multiple useful 
links and proper benchmark tests so users can decide if they want to use these tools as alternative to the traditional ones that
are already present.

## Building

On **Windows**: Visual Studio 2022 or newer (or Build Tools for Visual Studio) with the
"Desktop development with C++" workload, plus the .NET 10 SDK:

```powershell
.\build.ps1          # native engine, .NET solution, all tests
.\build.ps1 -Clean   # start the native build from scratch
```

The script finds Visual Studio itself (through `vswhere`), loads its x64 compiler environment and uses
its bundled CMake and Ninja, so it runs from any PowerShell window. zlib is downloaded and built
automatically (no git needed). The native DLL goes to `artifacts\native\` and is copied next to
`DBPFX.dll` in every project that references it. On .NET 10 the library also finds it under
`runtimes\<rid>\native\` (NuGet layout).

On **Linux** & **macOS**: `./build.sh`. Requires `zlib` development headers to compile.
