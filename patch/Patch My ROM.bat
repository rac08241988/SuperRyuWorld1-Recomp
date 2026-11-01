@echo off
setlocal
title Patch My ROM - Super Ryu World 1
rem ============================================================
rem  Patch My ROM - makes "Super Ryu World 1.sfc" from a clean Super Mario
rem  World (USA) ROM, using the official patch and Floating IPS
rem  (flips\flips.exe) in this folder.
rem  Drag your ROM onto this file, or double-click it and pick
rem  the ROM in the window that opens.
rem ============================================================
set "PMR_BAT=%~f0"
set "PMR_DIR=%~dp0"
set "PMR_ROM=%~1"
set "PMR_PS=powershell.exe"
where pwsh.exe >nul 2>&1 && set "PMR_PS=pwsh.exe"
"%PMR_PS%" -NoProfile -ExecutionPolicy Bypass -STA -Command "$s=[IO.File]::ReadAllText($env:PMR_BAT); $i=$s.IndexOf('#'+'#PS_BEGIN'); Invoke-Expression $s.Substring($i)"
set "PMR_RC=%ERRORLEVEL%"
echo.
if not defined PMR_NOPAUSE pause
exit /b %PMR_RC%

##PS_BEGIN
$ErrorActionPreference = 'Stop'
$Game      = 'Super Ryu World 1'
$PatchName = 'Super Ryu World 1.bps'
$CleanCrc  = [uint32]'0xB19ED489'
$GameCrc   = [uint32]'0xD6A6317E'
function Say($m, $c = 'Gray') { Write-Host $m -ForegroundColor $c }
function Fail($m) { Write-Host ''; Write-Host $m -ForegroundColor Red; exit 1 }
try {
  Add-Type -TypeDefinition @"
public static class PmrCrc {
  public static uint Of(byte[] b) {
    uint[] t = new uint[256];
    for (uint n = 0; n < 256; n++) { uint c = n; for (int k = 0; k < 8; k++) c = ((c & 1) != 0) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); t[n] = c; }
    uint r = 0xFFFFFFFFu;
    foreach (byte x in b) r = t[(r ^ x) & 0xFF] ^ (r >> 8);
    return r ^ 0xFFFFFFFFu;
  }
}
"@
  $dir   = $env:PMR_DIR
  $flips = Join-Path $dir 'flips\flips.exe'
  $patch = Join-Path $dir $PatchName
  Say "Patch My ROM - $Game" 'Cyan'
  Say ''
  if (-not (Test-Path -LiteralPath $flips)) { Fail "flips\flips.exe is missing from this folder ($dir). Re-download the game zip." }
  if (-not (Test-Path -LiteralPath $patch)) { Fail "The patch file '$PatchName' is missing from this folder ($dir). Re-download the game zip." }

  $rom = $env:PMR_ROM
  if ([string]::IsNullOrWhiteSpace($rom)) {
    Say 'Pick your clean Super Mario World (USA) ROM in the window that opens...'
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title  = 'Pick your clean Super Mario World (USA) ROM'
    $dlg.Filter = 'SNES ROM (*.sfc;*.smc)|*.sfc;*.smc|All files (*.*)|*.*'
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Fail 'No ROM was picked, so nothing was changed.' }
    $rom = $dlg.FileName
  }
  if (-not (Test-Path -LiteralPath $rom -PathType Leaf)) { Fail "Can't find the file: $rom" }
  Say "ROM: $rom"

  $bad = "This isn't a clean Super Mario World (USA) ROM. You need the original USA version, unmodified."
  $bytes = [IO.File]::ReadAllBytes($rom)
  if (($bytes.Length % 1024) -eq 512) {
    Say 'Removing the 512-byte copier header...'
    $nb = New-Object byte[] ($bytes.Length - 512); [Array]::Copy($bytes, 512, $nb, 0, $nb.Length); $bytes = $nb
  }
  $crc = [PmrCrc]::Of($bytes)
  if ($crc -eq $GameCrc) {
    Fail ($bad + "`n(This file is already $Game. You don't need to patch it: just pick it in the game's first-run setup.)")
  }
  if ($bytes.Length -ne 524288 -or $crc -ne $CleanCrc) {
    Fail ($bad + "`n(Expected CRC32 B19ED489, 524,288 bytes. This file: CRC32 {0:X8}, {1:N0} bytes. Japanese, European and Super Mario All-Stars versions do not work.)" -f $crc, $bytes.Length)
  }
  Say 'Clean Super Mario World (USA) ROM found (CRC32 B19ED489).' 'Green'

  $tmp   = Join-Path ([IO.Path]::GetTempPath()) ('pmr-' + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $tmp | Out-Null
  $inRom  = Join-Path $tmp 'clean.sfc'
  $outTmp = Join-Path $tmp 'patched.sfc'
  [IO.File]::WriteAllBytes($inRom, $bytes)
  Say 'Applying the patch with Floating IPS...'
  $q = { param($p) '"' + $p + '"' }
  $proc = Start-Process -FilePath $flips -ArgumentList @('--apply', '--exact', (& $q $patch), (& $q $inRom), (& $q $outTmp)) -Wait -PassThru -WindowStyle Hidden
  if (-not (Test-Path -LiteralPath $outTmp)) { Fail "Floating IPS could not apply the patch (exit code $($proc.ExitCode))." }
  $outBytes = [IO.File]::ReadAllBytes($outTmp)
  $outCrc = [PmrCrc]::Of($outBytes)
  if ($outCrc -ne $GameCrc) { Fail ("The patched ROM came out wrong (CRC32 {0:X8}, expected {1:X8}). Nothing was saved." -f $outCrc, $GameCrc) }

  $dest = Join-Path $dir ($Game + '.sfc')
  try { [IO.File]::WriteAllBytes($dest, $outBytes) }
  catch {
    $dest = Join-Path ([Environment]::GetFolderPath('MyDocuments')) ($Game + '.sfc')
    [IO.File]::WriteAllBytes($dest, $outBytes)
  }
  Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
  Say ''
  Say ("It worked! Made {0} (CRC32 {1:X8})." -f $dest, $outCrc) 'Green'
  Say "Now open the game's exe and, in its first-run setup, click Browse ROM and pick:"
  Say "  $dest" 'Yellow'
  exit 0
} catch {
  Fail "Something went wrong: $($_.Exception.Message)"
}
