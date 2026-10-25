# Tests

End-to-end tests for the plugin's read and write paths. They load the built
`opusAmigaDisc.dll`, call its VFS exports the way Directory Opus does, and
check every image they change with tools that don't share any code with firy.

| File | What it is |
|---|---|
| `vfstest.cpp` | Test host: list, add, delete, extract, find, read and free-space calls against the DLL |
| `cbmcheck.py` | Consistency check for D64/D71/D81: BAM counts and bitmaps, directory and file chains, sectors used twice or used but marked free |
| `runtests.ps1` | The scenarios. Prints `PASS`/`FAIL` per check and a total |

Amiga images are checked with [amitools](https://github.com/cnvogelg/amitools)
(`xdfscan` validates the filesystem, `xdftool` reads files back); CBM file
contents are read back with the [`d64`](https://pypi.org/project/d64/) package.

## Running

```
python -m venv R:\venv
R:\venv\Scripts\pip install amitools d64
tests\build_vfstest.cmd R:\probe
powershell -File tests\runtests.ps1 -Dll <path to opusAmigaDisc.dll>
```

`runtests.ps1` expects the test images in `..\..\test-disks` (next to the
repo, not in it - they're copyrighted) and works on copies in `-Work`
(default `R:\t`, wiped on every run). The defaults for `-Tools` and `-Venv`
match the commands above.

The run covers D64/D71/D81, OFS/FFS/FFS+INTL floppies, an RDB-partitioned
HDF, read-only DMS and DCFS images, umlauts in Windows paths and in names
inside images, full disks, replacing, directory growth and empty files.
