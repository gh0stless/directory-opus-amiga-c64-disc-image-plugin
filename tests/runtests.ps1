# End-to-end tests: real plugin DLL via vfstest.exe, images validated by independent tools
param(
    [string]$Dll = 'R:\build\opusAmigaDisc\Release\opusAmigaDisc.dll',  # plugin under test
    [string]$Work = 'R:\t',                                              # scratch folder, wiped
    [string]$Tools = 'R:\probe',                                         # vfstest.exe
    [string]$Venv = 'R:\venv',                                           # python venv with amitools + d64
    [string]$Images = (Join-Path $PSScriptRoot '..\..\test-disks')       # original test images
)
$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$OutputEncoding = [Text.Encoding]::UTF8

$dll = $Dll
$vt = Join-Path $Tools 'vfstest.exe'
$py = Join-Path $Venv 'Scripts\python.exe'
$cbm = Join-Path $PSScriptRoot 'cbmcheck.py'
$xdfscan = Join-Path $Venv 'Scripts\xdfscan.exe'
$xdftool = Join-Path $Venv 'Scripts\xdftool.exe'
$src = $Images
$root = $Work

$script:pass = 0; $script:fail = 0
function Check($ok, $what) {
    if ($ok) { $script:pass++; Write-Output "PASS  $what" } else { $script:fail++; Write-Output "FAIL  $what" }
}
function VT { & $vt $dll @args 2>&1 | Out-String }
function Hash($p) { if (Test-Path -LiteralPath $p) { (Get-FileHash -LiteralPath $p -Algorithm SHA1).Hash } else { 'missing' } }
function CbmOk($img) { $o = & $py $cbm $img | Out-String; return ($LASTEXITCODE -eq 0), $o }
# xdfscan prints one status line per image: "ok", or "E011 NOK" / "nofs" / "w002" on problems
function AdfOk($img) {
    $o = & $xdfscan -l 2 $img 2>&1 | Out-String
    $ok = ($o -match '\sok\s') -and ($o -notmatch 'NOK|nofs|E\d{3}|w\d{3}')
    if (-not $ok) { $o += (& $xdfscan -l 1 $img 2>&1 | Out-String) }
    return $ok, $o
}

# --- fresh workspace
Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
$data = "$root\data"; New-Item -ItemType Directory -Force $data | Out-Null
$rnd = New-Object Random 42
function MakeFile($name, $size) { $b = New-Object byte[] $size; $rnd.NextBytes($b); [IO.File]::WriteAllBytes("$data\$name", $b) }
MakeFile 'small.prg' 100
MakeFile 'mid.seq' 5000
MakeFile 'exact254.prg' 254
MakeFile 'big.bin' 20000      # 79 CBM blocks: fits ass-kurs.d64's 107 free blocks
MakeFile 'huge.bin' 300000
MakeFile 'toolarge.bin' 2000000
MakeFile ([string][char]0x4B + [char]0xE4 + 'fer.txt') 1234           # Käfer.txt (Latin-1)
MakeFile ([string][char]0x65E5 + [char]0x672C + '.txt') 10               # 日本.txt (not Latin-1)
MakeFile ('x' * 40 + '.txt') 10
New-Item -ItemType Directory -Force "$data\toobig" | Out-Null                # same names, too large to fit
MakeFile 'toobig\mid.seq' 2000000
MakeFile 'toobig\big.bin' 2000000
New-Item -ItemType Directory -Force "$data\other" | Out-Null                 # same names, different content
MakeFile 'other\mid.seq' 4000
MakeFile 'other\big.bin' 15000
New-Item -ItemType File -Force "$data\leer.txt" | Out-Null
New-Item -ItemType Directory -Force "$data	oobig" | Out-Null
foreach ($n in 'mid.seq', 'big.bin') { $b = New-Object byte[] 2000000; $rnd.NextBytes($b); [IO.File]::WriteAllBytes("$data	oobig\$n", $b) }
$tree = "$data\L" + [char]0xF6 + 'sung'                                   # Lösung (folder)
New-Item -ItemType Directory -Force "$tree\Unter" | Out-Null
$b = New-Object byte[] 3000; $rnd.NextBytes($b); [IO.File]::WriteAllBytes("$tree\a.txt", $b)
$b = New-Object byte[] 70000; $rnd.NextBytes($b); [IO.File]::WriteAllBytes("$tree\Unter\b.bin", $b)

$umlautDir = "$root\J" + [char]0xF6 + 'rg'                               # image folder with umlaut
$outDir = "$root\Ausgabe " + [char]0xFC                                  # extract target with umlaut
New-Item -ItemType Directory -Force $umlautDir, $outDir | Out-Null

# ============================================================ CBM
foreach ($case in @(@{ f = 'ass-kurs.d64' }, @{ f = 'cbm-filebrowser.d71' }, @{ f = 'zak-mckracken+maniac-mansion+1d.ons.d81' })) {
    $name = $case.f
    $img = "$umlautDir\$name"
    Copy-Item "$src\$name" $img
    Write-Output "`n##### $name (in folder with umlaut)"

    $o = VT list $img; Check ($o -notmatch 'LIST FAILED') "$name list via umlaut host path"
    $before = ($o -split "`n" | Where-Object { $_ -match "^FILE" }).Count

    $o = VT add $img "$data\small.prg" "$data\mid.seq" "$data\exact254.prg" "$data\big.bin"
    Check ($o -match 'result=4') "$name add 4 files"
    $o = VT list $img
    Check (($o -match "SMALL\b") -and ($o -match "MID\b") -and ($o -match "BIG.BIN")) "$name new files listed after reopen"
    $ok, $rep = CbmOk $img; Check $ok "$name valid after add"; if (-not $ok) { $rep }

    foreach ($pair in @(@('SMALL', 'small.prg'), @('MID', 'mid.seq'), @('EXACT254', 'exact254.prg'), @('BIG.BIN', 'big.bin'))) {
        & $py $cbm $img --dump $pair[0] "$root\dump.bin" 2>$null
        Check ((Hash "$root\dump.bin") -eq (Hash "$data\$($pair[1])")) "$name $($pair[0]) content (read by d64 library)"
        $o = VT read "$img\$($pair[0])" "$root\read.bin"
        Check (((Hash "$root\read.bin") -eq (Hash "$data\$($pair[1])")) -and $o -match 'ok=1') "$name $($pair[0]) content (plugin read)"
    }

    $o = VT add $img "$data\leer.txt"; Check ($o -match 'result=4') "$name add empty file"
    $o = VT list $img; Check ($o -match "\tLEER.TXT\t") "$name empty file listed after reopen"
    $o = VT read "$img\LEER.TXT" "$root\read.bin"; Check (($o -match 'ok=1 bytes=0')) "$name empty file reads as 0 bytes"
    $ok, $rep = CbmOk $img; Check $ok "$name valid after empty file"; if (-not $ok) { $rep }
    $o = VT add $img "$data\toolarge.bin"; Check ($o -match 'result=3') "$name add too-large file fails"
    $ok, $rep = CbmOk $img; Check $ok "$name still valid after failed add"; if (-not $ok) { $rep }

    $nameBad = [string][char]0x4B + [char]0xE4 + 'fer.txt'
    $o = VT add $img "$data\$nameBad"; Check ($o -match 'result=3') "$name non-ASCII name rejected"
    $o = VT add $img "$data\$('x' * 40).txt"; Check ($o -match 'result=3') "$name over-long name rejected"
    $o = VT add $img "$tree"; Check ($o -match 'result=3') "$name folder rejected (no folders on CBM)"

    # replace same name
    $o = VT add $img "$data\small.prg"; Check ($o -match 'result=4') "$name re-add same name replaces"
    $o = VT list $img; Check ((($o -split "`n") -match "\tSMALL\t").Count -eq 1) "$name only one SMALL after replace"

    # replacing with a file that doesn't fit must keep the old one
    $o = VT add $img "$data\toobig\mid.seq"; Check ($o -match 'result=3') "$name replace with too-large file refused"
    $o = VT read "$img\MID" "$root\read.bin"; Check ((Hash "$root\read.bin") -eq (Hash "$data\mid.seq")) "$name old MID intact after refused replace"
    $o = VT add $img "$data\other\mid.seq"; Check ($o -match 'result=4') "$name replace MID with different content"
    $o = VT read "$img\MID" "$root\read.bin"; Check ((Hash "$root\read.bin") -eq (Hash "$data\other\mid.seq")) "$name MID has the new content"
    $ok, $rep = CbmOk $img; Check $ok "$name valid after replace"; if (-not $ok) { $rep }

    # delete
    $o = VT del "$img\SMALL" "$img\BIG.BIN"; Check ($o -match 'result=4') "$name delete 2 files"
    $o = VT list $img; Check (($o -notmatch "\tSMALL\t") -and ($o -notmatch "BIG.BIN")) "$name deleted files gone after reopen"
    $ok, $rep = CbmOk $img; Check $ok "$name valid after delete"; if (-not $ok) { $rep }

    # fill the directory past a sector boundary (forces directory growth)
    $many = @(); 1..20 | ForEach-Object { $p = "$data\f$_.prg"; [IO.File]::WriteAllBytes($p, [byte[]](1..10)); $many += $p }
    $o = VT add $img @many; Check ($o -match 'result=4') "$name add 20 small files"
    $ok, $rep = CbmOk $img; Check $ok "$name valid after directory growth"; if (-not $ok) { $rep }
    $o = VT list $img; Check ($o -match "\tF20\t") "$name F20 listed"

    $o = VT extract $outDir "$img\MID"
    Check ((Hash "$outDir\MID") -eq (Hash "$data\other\mid.seq")) "$name extract into umlaut folder"
    $o = VT free $img; Write-Output "      $($o.Trim())"
    $o = VT find "$img\F1*"; Check ((($o -split "`n") -match '^found').Count -eq 11) "$name find F1* (F1, F10-F19)"
    $o = VT find "$img\NOPE*"; Check ($o -match 'no match') "$name find without match returns no handle"
}

# ============================================================ ADF
# the two book disks are OFS; FFS variants are freshly formatted by xdftool
foreach ($fmt in @('ffs', 'ffs+intl')) {
    & $xdftool "$root\fresh-$($fmt -replace '\+','-').adf" create + format "Fresh" $fmt 2>&1 | Out-Null
}
$dcHash = $null
& $xdftool "$root\dircache.adf" create + format "DC" ffs+dc 2>&1 | Out-Null
foreach ($name in @('amiga-assembler-buch.adf', 'spieleprogrammierung-in-assembler-book-disk.adf', 'fresh-ffs.adf', 'fresh-ffs-intl.adf')) {
    $img = "$umlautDir\$name"
    if (Test-Path "$src\$name") { Copy-Item "$src\$name" $img } else { Move-Item "$root\$name" $img }
    $info = & $xdftool $img info 2>&1 | Out-String
    Write-Output "`n##### $name"
    $ok, $rep = AdfOk $img; Check $ok "$name valid before (baseline)"; if (-not $ok) { $rep }

    $o = VT add $img "$data\small.prg" "$data\big.bin" "$data\huge.bin" "$data\leer.txt" "$data\$([string][char]0x4B + [char]0xE4 + 'fer.txt')" $tree
    Check ($o -match 'result=4') "$name add files + folder tree (incl. Latin-1 names)"
    $ok, $rep = AdfOk $img; Check $ok "$name valid after add (xdfscan)"; if (-not $ok) { $rep }

    foreach ($pair in @(@('small.prg', "$data\small.prg"), @('big.bin', "$data\big.bin"), @('huge.bin', "$data\huge.bin"), @('leer.txt', "$data\leer.txt"), @("L$([char]0xF6)sung/Unter/b.bin", "$tree\Unter\b.bin"))) {
        Remove-Item "$root\x.bin" -ErrorAction SilentlyContinue
        & $xdftool $img read $pair[0] "$root\x.bin" 2>&1 | Out-Null
        Check ((Hash "$root\x.bin") -eq (Hash $pair[1])) "$name $($pair[0]) content (read by xdftool)"
    }
    $k = [string][char]0x4B + [char]0xE4 + 'fer.txt'
    $o = VT read "$img\$k" "$root\read.bin"
    Check ((Hash "$root\read.bin") -eq (Hash "$data\$k")) "$name Käfer.txt via plugin (Latin-1 lookup)"
    $o = VT list "$img\L$([char]0xF6)sung"; Check ($o -match "a.txt" -and $o -match "Unter") "$name browse into Lösung folder"

    $o = VT add $img "$data\$([string][char]0x65E5 + [char]0x672C + '.txt')"; Check ($o -match 'result=3') "$name non-Latin-1 name rejected"
    $o = VT add $img "$data\toolarge.bin"; Check ($o -match 'result=3') "$name too large rejected"
    $ok, $rep = AdfOk $img; Check $ok "$name valid after failed adds"; if (-not $ok) { $rep }

    # same name in different case replaces (AmigaDOS names are case-insensitive)
    Copy-Item "$data\small.prg" "$root\SMALL.PRG" -Force
    $o = VT add $img "$root\SMALL.PRG"; Check ($o -match 'result=4') "$name add SMALL.PRG over small.prg"
    $o = VT list $img; Check ((($o -split "`n") -match "(?i)\tsmall\.prg\t").Count -eq 1) "$name only one small.prg entry"
    # merging a folder onto an existing one
    $o = VT add $img $tree; Check ($o -match 'result=4') "$name copy folder again (merge/replace)"
    $ok, $rep = AdfOk $img; Check $ok "$name valid after replace/merge"; if (-not $ok) { $rep }

    $o = VT add $img "$data\toobig\big.bin"; Check ($o -match 'result=3') "$name replace with too-large file refused"
    $o = VT read "$img\big.bin" "$root\read.bin"; Check ((Hash "$root\read.bin") -eq (Hash "$data\big.bin")) "$name old big.bin intact after refused replace"
    $o = VT add $img "$data\other\big.bin"; Check ($o -match 'result=4') "$name replace big.bin with different content"
    $o = VT read "$img\big.bin" "$root\read.bin"; Check ((Hash "$root\read.bin") -eq (Hash "$data\other\big.bin")) "$name big.bin has the new content"
    $ok, $rep = AdfOk $img; Check $ok "$name valid after replace"; if (-not $ok) { $rep }
    $o = VT del "$img\huge.bin" "$img\L$([char]0xF6)sung"; Check ($o -match 'result=4') "$name delete file + folder tree"
    $o = VT list $img; Check (($o -notmatch 'huge.bin') -and ($o -notmatch "L$([char]0xF6)sung")) "$name deleted entries gone"
    $ok, $rep = AdfOk $img; Check $ok "$name valid after delete"; if (-not $ok) { $rep }
    $o = VT extract $outDir "$img\big.bin"; Check ((Hash "$outDir\big.bin") -eq (Hash "$data\other\big.bin")) "$name extract into umlaut folder"
    $o = VT free $img; Write-Output "      $($o.Trim())"
}

# ============================================================ HDF partition
$img = "$root\workbench-311.hdf"; Copy-Item "$src\workbench-311.hdf" $img
Write-Output "`n##### workbench-311.hdf"
$o = VT list $img; Write-Output $o
$part = (($o -split "`n") | Where-Object { $_ -match '^DIR' } | Select-Object -First 1) -split "`t" | Select-Object -Index 2
$o = VT add "$img\$part" "$data\big.bin" $tree; Check ($o -match 'result=4') "hdf add into partition $part"
$o = VT read "$img\$part\big.bin" "$root\read.bin"; Check ((Hash "$root\read.bin") -eq (Hash "$data\big.bin")) "hdf big.bin read back after reopen"
$o = VT del "$img\$part\big.bin" "$img\$part\L$([char]0xF6)sung"; Check ($o -match 'result=4') "hdf delete in partition"
$ok, $rep = AdfOk $img; Check $ok "hdf valid (xdfscan)"; if (-not $ok) { $rep }

# ============================================================ read-only formats
foreach ($name in @('arexx-buch.dms', 'dircache.adf')) {
    $img = "$root\$name"   # dircache.adf was formatted above, the DMS comes from test-disks
    if (-not (Test-Path $img)) { Copy-Item "$src\$name" $img }
    $h1 = Hash $img
    $o = VT add $img "$data\small.prg"; Check ($o -match 'result=3') "$name add refused (read-only)"
    $o = VT list $img; Check ($o -notmatch 'small.prg') "$name unchanged listing"
    Check ((Hash $img) -eq $h1) "$name file untouched"
}

Write-Output "`n=== $script:pass passed, $script:fail failed"
