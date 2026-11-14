# Akai Fire custom firmware: install / restore tool for Windows.
# Started by "Install custom firmware.bat" (install) or "Restore original firmware.bat" (restore).
#
# install: backs up the WHOLE chip (read twice, compared), then writes the custom firmware into the app area
#          (0x08008000, 96 KB) and reads the chip back to check it.
# restore: writes a backup's app area + pattern area (0x08008000-0x0803FFFF) back and checks it.
# Neither ever writes Akai's bootloader (0x08000000) or its settings (0x08004000), so the Fire can always be put
# back into update mode with the button combo (STEP + NOTE + DRUM + PERFORM held while plugging in).
#
# install also keeps the FIRST backup of a Fire still on Akai's firmware as backups\ORIGINAL_Akai_firmware.bin (never
# overwritten); restore puts that file back. -File <backup> restores any other backup instead (advanced use).
param([Parameter(Mandatory = $true)][ValidateSet('install', 'restore')][string]$Mode, [string]$File)

$ErrorActionPreference = 'Stop'
$Here    = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root    = Split-Path -Parent $Here
$Dfu     = Join-Path $Here 'dfu-util.exe'
$Drivers = Join-Path $Here 'qmk_driver_installer.exe'
$FwBin   = Get-ChildItem -Path $Here -Filter 'fire_custom_*.bin' | Select-Object -First 1
$BackDir = Join-Path $Root 'backups'
$Original = Join-Path $BackDir 'ORIGINAL_Akai_firmware.bin'
$ChipSize = 262144          # STM32F401RC: 256 KB
$AppAddr  = 0x08008000
$AppSize  = 98304           # sectors 2-4
$RestSize = 229376          # sectors 2-5 (app + pattern area)
$Token    = [Text.Encoding]::ASCII.GetBytes('EndOfFlashToken')

function Say($t, $c = 'Gray') { Write-Host $t -ForegroundColor $c }
function Fail($t) {
    Say ''
    Say "STOPPED: $t" 'Red'
    Say 'Nothing after this point was done. Your Fire is safe: unplug it and plug it back in.' 'Yellow'
    exit 1
}
function Choose($q, [string[]]$letters) {   # returns the lower-case letter typed; nothing else counts as an answer
    while ($true) {
        try { $Host.UI.RawUI.FlushInputBuffer() } catch {}     # drop keys pressed earlier (a stray Enter is not an answer)
        $a = (Read-Host $q).Trim().ToLower()
        if ($letters -contains $a) { return $a }
        if ($a.Length -ge 1 -and $a -notmatch '^\d' -and $letters -contains $a.Substring(0, 1)) { return $a.Substring(0, 1) }
    }
}

# ---------------------------------------------------------------- MIDI: ask the running Fire to enter update mode
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FireMidi {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct MIDIOUTCAPS {
        public ushort wMid, wPid; public uint vDriverVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szPname;
        public ushort wTechnology, wVoices, wNotes, wChannelMask; public uint dwSupport;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct MIDIHDR {
        public IntPtr lpData; public uint dwBufferLength, dwBytesRecorded; public IntPtr dwUser;
        public uint dwFlags; public IntPtr lpNext, reserved; public uint dwOffset;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] public IntPtr[] dwReserved;
    }
    [DllImport("winmm.dll")] static extern uint midiOutGetNumDevs();
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern uint midiOutGetDevCapsW(UIntPtr id, ref MIDIOUTCAPS caps, uint size);
    [DllImport("winmm.dll")] static extern uint midiOutOpen(out IntPtr h, uint id, IntPtr cb, IntPtr inst, uint flags);
    [DllImport("winmm.dll")] static extern uint midiOutPrepareHeader(IntPtr h, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint midiOutUnprepareHeader(IntPtr h, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint midiOutLongMsg(IntPtr h, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint midiOutClose(IntPtr h);
    [DllImport("kernel32.dll")] static extern IntPtr GetStdHandle(int n);
    [DllImport("kernel32.dll")] static extern bool GetConsoleMode(IntPtr h, out uint m);
    [DllImport("kernel32.dll")] static extern bool SetConsoleMode(IntPtr h, uint m);

    // A click inside the console window starts a text selection ("QuickEdit"), which FREEZES the script until a key
    // is pressed. Turn QuickEdit off for this window (0x40 = QuickEdit, 0x80 = extended flags must be set to change it).
    public static void NoQuickEdit() {
        IntPtr h = GetStdHandle(-10); uint m;
        if (GetConsoleMode(h, out m)) SetConsoleMode(h, (m & ~0x40u) | 0x80u);
    }

    public static string[] Names() {
        uint n = midiOutGetNumDevs(); string[] r = new string[n];
        for (uint i = 0; i < n; i++) {
            MIDIOUTCAPS c = new MIDIOUTCAPS();
            midiOutGetDevCapsW(new UIntPtr(i), ref c, (uint)Marshal.SizeOf(typeof(MIDIOUTCAPS)));
            r[i] = c.szPname;
        }
        return r;
    }
    // Returns "" on success, else what went wrong.
    public static string SendSysex(uint id, byte[] msg) {
        IntPtr h; uint e = midiOutOpen(out h, id, IntPtr.Zero, IntPtr.Zero, 0);
        if (e != 0) return "could not open the MIDI port (error " + e + "; is FL Studio or another music program using it?)";
        IntPtr data = Marshal.AllocHGlobal(msg.Length);
        Marshal.Copy(msg, 0, data, msg.Length);
        MIDIHDR hd = new MIDIHDR(); hd.lpData = data; hd.dwBufferLength = (uint)msg.Length;
        hd.dwBytesRecorded = (uint)msg.Length; hd.dwReserved = new IntPtr[8];
        int sz = Marshal.SizeOf(typeof(MIDIHDR));
        IntPtr hdr = Marshal.AllocHGlobal(sz);
        Marshal.StructureToPtr(hd, hdr, false);
        string err = "";
        e = midiOutPrepareHeader(h, hdr, (uint)sz);
        if (e != 0) err = "prepare failed (" + e + ")";
        else {
            e = midiOutLongMsg(h, hdr, (uint)sz);
            if (e != 0) err = "send failed (" + e + ")";
            int flagsOff = (int)Marshal.OffsetOf(typeof(MIDIHDR), "dwFlags");
            for (int i = 0; i < 100 && e == 0; i++) {           // wait up to 2 s for MHDR_DONE
                if ((Marshal.ReadInt32(hdr, flagsOff) & 1) != 0) break;
                System.Threading.Thread.Sleep(20);
            }
            midiOutUnprepareHeader(h, hdr, (uint)sz);
        }
        midiOutClose(h);
        Marshal.FreeHGlobal(hdr); Marshal.FreeHGlobal(data);
        return err;
    }
}
'@

# ---------------------------------------------------------------- USB / update mode
function DfuPresent {   # Windows sees the STM32 update-mode device (driver or not)
    return [bool](Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like 'USB\VID_0483&PID_DF11*' })
}
function RunDfu {       # run dfu-util, return its output as text (stderr included). Windows PowerShell 5.1 turns a
    # native program's stderr into errors, and with 'Stop' those end the script: relax it for this call only.
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { $out = & $Dfu @args 2>&1 | ForEach-Object { "$_" } | Out-String; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $old }
    $script:DfuExit = $code
    return $out
}
function DfuUsable {    # dfu-util can talk to it (needs the WinUSB driver)
    $out = RunDfu -l
    return ($out -match '\[0483:df11\]')
}
function WaitFor([scriptblock]$cond, [int]$secs) {
    for ($i = 0; $i -lt $secs * 2; $i++) { if (& $cond) { return $true }; Start-Sleep -Milliseconds 500 }
    return $false
}

function Enter-UpdateMode {
    if (DfuPresent) { Say 'The Fire is already in update mode.'; }
    else {
        $names = [FireMidi]::Names()
        $id = -1
        # Akai's firmware ("FL STUDIO FIRE"), ours up to v0.35b ("ORC FW seq"), ours since the FLAME rename (v0.36+).
        # FLAME has two ports. Windows 10 names them after the device, "FLAME" (the sequencer, the one that takes the
        # update-mode message) and "MIDIOUT2 (FLAME)" (the DAW port); newer Windows MIDI may use the port names
        # "FLAME seq" / "FLAME DAW". So FLAME matches the WHOLE name only ("2- " = Windows' prefix for a second
        # Fire): never the DAW port, never other makers' gear with FLAME in the name (Flame Instruments).
        $pat = 'FL STUDIO FIRE|ORC FW|^(\d+- )?FLAME( seq)?$'
        for ($i = 0; $i -lt $names.Length; $i++) { if ($names[$i] -match $pat) { $id = $i; break } }
        if ($id -ge 0) {
            Say "Found the Fire ('$($names[$id])'). Asking it to switch to update mode (the screen goes dark)..."
            $sx = [byte[]](0xF0, 0x47, 0x7F, 0x43, 0x72, 0x00, 0x00, 0xF7)
            $err = [FireMidi]::SendSysex([uint32]$id, $sx)
            if ($err) { Say "  That did not work: $err" 'Yellow' }
        } else {
            Say 'No Fire found as a MIDI device.' 'Yellow'
        }
        if (-not (WaitFor { DfuPresent } 10)) {
            Say ''
            Say 'Please put the Fire into update mode by hand:' 'Cyan'
            Say '  1. Unplug the Fire''s USB cable.'
            Say '  2. Hold down STEP + NOTE + DRUM + PERFORM (the four buttons bottom left).'
            Say '  3. Keep holding them and plug the cable back in. Let go after 2 seconds.'
            Say '     The screen and pads stay dark: that is update mode.'
            Say 'Waiting up to 2 minutes...'
            if (-not (WaitFor { DfuPresent } 120)) { Fail 'the Fire never showed up in update mode.' }
        }
        Say 'The Fire is in update mode.' 'Green'
    }
    if (-not (WaitFor { DfuUsable } 3)) {
        Say ''
        Say 'Windows needs a one-time USB driver (WinUSB) for update mode.' 'Cyan'
        Say 'A Windows "allow changes" box will pop up next: click Yes. This takes up to a minute.'
        $list = Join-Path $env:TEMP 'fire_drivers.txt'
        Set-Content -Path $list -Encoding ASCII -Value 'winusb,STM32 Bootloader,0483,DF11,6d98a87f-4ecf-464d-89ed-8c684d857a75'
        try { Start-Process -FilePath $Drivers -ArgumentList @('--force', "`"$list`"") -Verb RunAs -Wait }
        catch { Fail 'the driver install was cancelled.' }
        if (-not (WaitFor { DfuUsable } 20)) { Fail 'the driver went in but the flashing tool still cannot see the Fire. Unplug it, plug it back in (holding the four buttons) and run this again.' }
        Say 'Driver ready.' 'Green'
    }
}

function Read-Chip([string]$file) {
    if (Test-Path $file) { Remove-Item $file -Force }
    $out = RunDfu -a 0 -s ('0x08000000:{0}' -f $ChipSize) -U $file
    if (-not (Test-Path $file) -or (Get-Item $file).Length -ne $ChipSize) {
        Say $out 'DarkGray'; Fail 'could not read the Fire''s memory.'
    }
    return [IO.File]::ReadAllBytes($file)
}
function Same([byte[]]$a, [int]$ao, [byte[]]$b, [int]$bo, [int]$n) {
    for ($i = 0; $i -lt $n; $i++) { if ($a[$ao + $i] -ne $b[$bo + $i]) { return $false } }
    return $true
}
function HasToken([byte[]]$chip) { return (Same $chip 0x1FFF0 $Token 0 $Token.Length) }
function IsAkaiApp([byte[]]$chip) {   # our firmware carries an "ORCBUILD=" tag in its app area; Akai's does not
    return ([Text.Encoding]::ASCII.GetString($chip, 0x8000, 0x18000).IndexOf('ORCBUILD=') -lt 0)
}
function Save-Backup([string]$suffix) {   # read the chip twice, compare, keep it; returns @(path, bytes)
    New-Item -ItemType Directory -Force -Path $BackDir | Out-Null
    $path = Join-Path $BackDir ('fire_backup_{0}{1}.bin' -f (Get-Date -Format 'yyyy-MM-dd_HHmmss'), $suffix)
    $x = Read-Chip $path
    $y = Read-Chip (Join-Path $env:TEMP 'fire_check.bin')
    if (-not (Same $x 0 $y 0 $ChipSize)) { Fail 'the two reads of the Fire did not match.' }
    if (-not (HasToken $x)) { Fail 'the Fire''s memory does not look like a working Fire (no end marker). Nothing was written.' }
    return @($path, $x)
}
function IsBlank([byte[]]$b, [int]$o, [int]$n) {
    for ($i = 0; $i -lt $n; $i++) { if ($b[$o + $i] -ne 0xFF) { return $false } }
    return $true
}
function Write-Area([string]$file, [int]$len) {
    $out = RunDfu -a 0 -s ('0x{0:X8}:{1}' -f $AppAddr, $len) -D $file
    if ($script:DfuExit -ne 0) { Say $out 'DarkGray'; return $false }
    return $true
}
function Leave-UpdateMode {
    RunDfu -a 0 -s 0x08000000:leave | Out-Null
    Start-Sleep -Seconds 4
    if (DfuPresent) { Say 'If the Fire stays dark, unplug it and plug it back in (no buttons held).' 'Yellow' }
}

# ---------------------------------------------------------------- the two jobs
try { [FireMidi]::NoQuickEdit() } catch {}
Say ''
if ($Mode -eq 'install') {
    if (-not $FwBin) { Fail 'the firmware file is missing from the "files" folder.' }
    $fw = [IO.File]::ReadAllBytes($FwBin.FullName)
    if ($fw.Length -ne $AppSize -or -not (Same $fw ($AppSize - 16) $Token 0 $Token.Length)) { Fail 'the firmware file is damaged (wrong size or end marker).' }
    Say "Akai Fire custom firmware installer ($($FwBin.Name))" 'Cyan'
    Say 'Step 1 of 3: back up what is on your Fire now. Step 2: install. Step 3: check.'
    Say 'Plug the Fire into this computer (close FL Studio or any other music program first).'
    Say ''
    Enter-UpdateMode

    Say ''
    Say 'Step 1: reading your Fire''s whole memory (twice, to be sure)...'
    $r = Save-Backup ''; $backup = $r[0]; $a = $r[1]
    Say "Backup saved: $backup" 'Green'
    if (IsAkaiApp $a) {
        if (-not (Test-Path $Original)) {
            Copy-Item $backup $Original
            Say "Your Fire's original Akai firmware is saved as: $Original" 'Green'
            Say '  Keep this file. "Restore original Akai firmware.bat" puts it back whenever you want.'
        } else {
            Say "(The original Akai firmware was already saved earlier: $Original)"
        }
    } elseif (-not (Test-Path $Original)) {
        Say 'Note: this Fire was already running a custom firmware, so this backup is not Akai''s original.' 'Yellow'
    }

    $format = $false
    if (-not (IsBlank $a 0x20000 0x20000)) {   # a Fire on Akai's firmware has this area empty: no question then
        Say ''
        Say 'This Fire already has saved patterns from an earlier install of this firmware.' 'Yellow'
        Say '  K = keep them (the firmware loads them)'
        Say '  F = format: erase them and start with no patterns'
        Say '  C = cancel: stop here, nothing written'
        Say 'Either way they are in the backup you just made, so Restore can bring them back.'
        $ans = Choose 'Type K, F or C, then Enter' @('k', 'f', 'c')
        if ($ans -eq 'c') { Fail 'cancelled. Nothing was written.' }
        $format = ($ans -eq 'f')
    }

    Say ''
    Say 'Step 2: installing the custom firmware (about 10 seconds, do not unplug)...'
    $img = $FwBin.FullName; $len = $AppSize
    if ($format) {   # one write: the firmware followed by an empty (0xFF) pattern area
        $full = New-Object byte[] $RestSize
        for ($i = $AppSize; $i -lt $RestSize; $i++) { $full[$i] = 0xFF }
        [Array]::Copy($fw, 0, $full, 0, $AppSize)
        $img = Join-Path $env:TEMP 'fire_install_format.bin'; [IO.File]::WriteAllBytes($img, $full); $len = $RestSize
        Say '(and erasing the saved patterns)'
    }
    if (-not (Write-Area $img $len)) { Fail 'writing failed. Unplug, hold STEP + NOTE + DRUM + PERFORM, plug in, and run Install again (or Restore).' }

    Say 'Step 3: checking...'
    $c = Read-Chip (Join-Path $env:TEMP 'fire_check.bin')
    if (-not (Same $c 0 $a 0 0x8000)) { Fail 'CHECK FAILED: the bootloader area changed (this should never happen). Run Restore.' }
    if (-not (Same $c 0x8000 $fw 0 $AppSize)) { Fail 'CHECK FAILED: the firmware did not write correctly. Run Install again or Restore.' }
    if ($format -and -not (IsBlank $c 0x20000 0x20000)) { Fail 'CHECK FAILED: the pattern area did not erase. Run Install again or Restore.' }
    if (-not $format -and -not (Same $c 0x20000 $a 0x20000 0x20000)) { Fail 'CHECK FAILED: the saved patterns changed. Run Restore.' }
    Say 'Checked: the custom firmware is on the Fire, Akai''s bootloader untouched.' 'Green'
    Leave-UpdateMode
    Say ''
    Say 'DONE. The Fire should now light up running the custom firmware.' 'Green'
}
else {
    if ($File) {
        Say 'Akai Fire: restore a backup' 'Cyan'
        if (-not (Test-Path $File)) { Fail "no such file: $File" }
        $pick = Get-Item $File
    } elseif (Test-Path $Original) {
        Say 'Akai Fire: put the original Akai firmware back' 'Cyan'
        $pick = Get-Item $Original
    } else {
        Say 'Akai Fire: put the original Akai firmware back' 'Cyan'
        $list = @(Get-ChildItem -Path $BackDir -Filter 'fire_backup_*.bin' -ErrorAction SilentlyContinue | Sort-Object Name)
        if ($list.Count -eq 0) { Fail "no backup found in $BackDir. The original is saved by Install the first time it runs on a Fire with Akai's firmware." }
        Say "There is no ORIGINAL_Akai_firmware.bin in $BackDir (this Fire was not on Akai's firmware" 'Yellow'
        Say 'when Install first ran here). These backups exist, oldest first:' 'Yellow'
        for ($i = 0; $i -lt $list.Count; $i++) { Say ("  {0}. {1}" -f ($i + 1), $list[$i].Name) }
        $nums = @(1..$list.Count | ForEach-Object { "$_" }) + 'c'
        $n = Choose 'Type a number to restore it, or C to cancel, then Enter' $nums
        if ($n -eq 'c') { Fail 'cancelled. Nothing was written.' }
        $pick = $list[[int]$n - 1]
    }
    $bk = [IO.File]::ReadAllBytes($pick.FullName)
    if ($bk.Length -ne $ChipSize -or -not (HasToken $bk)) { Fail "$($pick.Name) is not a complete Fire backup." }
    Say "Restoring $($pick.Name)"
    Say 'Plug the Fire into this computer (close FL Studio or any other music program first).'
    Say ''
    Enter-UpdateMode

    Say ''
    Say 'First, backing up what is on the Fire now (your patterns included)...'
    $r = Save-Backup '_before_restore'
    Say "Saved: $($r[0])" 'Green'
    if ((IsBlank $bk 0x20000 0x20000) -and -not (IsBlank $r[1] 0x20000 0x20000)) {
        Say 'Restoring clears the custom firmware''s saved patterns (they are in the backup just saved).'
    }

    $tmp = Join-Path $env:TEMP 'fire_restore.bin'
    $part = New-Object byte[] $RestSize
    [Array]::Copy($bk, 0x8000, $part, 0, $RestSize)
    [IO.File]::WriteAllBytes($tmp, $part)
    Say ''
    Say 'Writing the backup back (about 20 seconds, do not unplug)...'
    if (-not (Write-Area $tmp $RestSize)) { Fail 'writing failed. Unplug, hold STEP + NOTE + DRUM + PERFORM, plug in, and run Restore again.' }
    Say 'Checking...'
    $c = Read-Chip (Join-Path $env:TEMP 'fire_check.bin')
    if (-not (Same $c 0x8000 $bk 0x8000 $RestSize)) { Fail 'CHECK FAILED: the restore did not write correctly. Run Restore again.' }
    if (-not (Same $c 0 $bk 0 0x8000)) { Say 'Note: Akai''s settings area differs from the backup (it was never written by this tool).' 'Yellow' }
    Say 'Checked: the Fire matches the backup.' 'Green'
    Leave-UpdateMode
    Say ''
    if ($pick.FullName -eq (Get-Item $Original -ErrorAction SilentlyContinue).FullName) { Say 'DONE. The Fire should now start up with Akai''s original firmware.' 'Green' }
    else { Say 'DONE. The Fire should now start up as it was when that backup was made.' 'Green' }
}
