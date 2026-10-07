#requires -Version 7.2
<#
Test-only collector for the Release static-image Engine harness.
Owns samples/summary/logs only; the child owns frames/events/heartbeat/result.
No elevation, counter configuration changes, power changes, or process-name kills.
Default is characterization, NOT authorization to run a canonical baseline.
#>
[CmdletBinding()]
param(
    [string]$Repository = (Join-Path $PSScriptRoot '../..'),
    [string]$BuildDirectory = 'build',
    [string]$Media = 'TestResources/Images/d51e6c28d76e72fc409a72159f582ada61a2add0.jpg',
    [ValidateRange(60, 86400)][double]$WarmupSeconds = 60,
    [ValidateRange(60, 86400)][double]$WarmupTimeoutSeconds = 180,
    [ValidateRange(1, 86400)][double]$MeasureSeconds = 120,
    [ValidateRange(1, 100)][double]$FramePeriodMs = 16,
    [ValidateRange(1, 60)][double]$SampleSeconds = 5,
    [switch]$SelfTest
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Get-MeasurementWindow($Timing, [long]$ExpectedFrequency) {
    if ($null -eq $Timing -or $Timing.timebase -ne 'QueryPerformanceCounter' -or $Timing.endpoints -ne 'inclusive' `
        -or [long]$Timing.frequency -ne $ExpectedFrequency -or $ExpectedFrequency -le 0 `
        -or $null -eq $Timing.start -or $null -eq $Timing.end) { throw 'Authoritative measurement window missing or incompatible' }
    $start = [long]::Parse([string]$Timing.start.counterTicks, $invariant)
    $end = [long]::Parse([string]$Timing.end.counterTicks, $invariant)
    if ($end -le $start) { throw 'Authoritative measurement end must follow start' }
    @{ startCounterTicks = $start; endCounterTicks = $end; frequency = $ExpectedFrequency }
}
function Counter-InMeasurementWindow($Ticks, $Window) {
    if ($null -eq $Ticks -or [string]$Ticks -eq 'NA' -or [string]::IsNullOrEmpty([string]$Ticks)) { return $false }
    $counter = [long]::Parse([string]$Ticks, $invariant)
    return $counter -ge $Window.startCounterTicks -and $counter -le $Window.endCounterTicks
}
function Select-MeasurementPoints($Rows, $Window) {
    # Queries are not atomic: require the whole acquisition envelope inside the
    # inclusive shared-QPC window, not merely a stale heartbeat phase or UTC.
    @($Rows | Where-Object { $_.requiredMetricsValidity -eq 'VALID' `
        -and (Counter-InMeasurementWindow $_.sampleQueryStartCounterTicks $Window) `
        -and (Counter-InMeasurementWindow $_.sampleCounterTicks $Window) `
        -and [long]$_.sampleQueryStartCounterTicks -le [long]$_.sampleCounterTicks })
}
function Select-MeasurementCpuIntervals($Rows, $Window) {
    @($Rows | Where-Object { $_.requiredMetricsValidity -eq 'VALID' -and $_.cpuValidity -eq 'VALID' `
        -and (Counter-InMeasurementWindow $_.cpuWindowStartQueryCounterTicks $Window) `
        -and (Counter-InMeasurementWindow $_.cpuWindowStartCounterTicks $Window) `
        -and (Counter-InMeasurementWindow $_.cpuQueryStartCounterTicks $Window) `
        -and (Counter-InMeasurementWindow $_.cpuSampleCounterTicks $Window) `
        -and [long]$_.cpuWindowStartQueryCounterTicks -le [long]$_.cpuWindowStartCounterTicks `
        -and [long]$_.cpuWindowStartCounterTicks -le [long]$_.cpuQueryStartCounterTicks `
        -and [long]$_.cpuQueryStartCounterTicks -le [long]$_.cpuSampleCounterTicks })
}
function Test-MeasurementSelectors {
    # Synthetic shared-counter window, including precision beyond double's
    # integer range. A stale heartbeat deliberately labels teardown measurement.
    $start = 10000000000000001L; $end = $start + 1000L
    $timing = @{ timebase = 'QueryPerformanceCounter'; endpoints = 'inclusive'; frequency = 1000L;
        start = @{ counterTicks = $start }; end = @{ counterTicks = $end } }
    $window = Get-MeasurementWindow $timing 1000L
    $rejected = $false
    try { $null = Get-MeasurementWindow $timing 999L } catch { $rejected = $true }
    if (-not $rejected) { throw 'Counter frequency mismatch must fail closed' }
    $rejected = $false
    try { $null = Get-MeasurementWindow $null 1000L } catch { $rejected = $true }
    if (-not $rejected) { throw 'Missing authoritative window must fail closed' }
    function Point-Row([string]$Id, [long]$Tick, [string]$Phase = 'measurement') {
        [pscustomobject]@{ id = $Id; phase = $Phase; requiredMetricsValidity = 'VALID';
            sampleQueryStartCounterTicks = $Tick; sampleCounterTicks = $Tick }
    }
    function Cpu-Row([string]$Id, [long]$From, [long]$To) {
        [pscustomobject]@{ id = $Id; phase = 'measurement'; cpuWindowStartPhase = 'measurement';
            requiredMetricsValidity = 'VALID'; cpuValidity = 'VALID';
            cpuWindowStartQueryCounterTicks = $From; cpuWindowStartCounterTicks = $From;
            cpuQueryStartCounterTicks = $To; cpuSampleCounterTicks = $To }
    }
    function Assert-Ids($Rows, [string]$Expected, [string]$Case) {
        $actual = @($Rows | Where-Object { $null -ne $_ } | ForEach-Object id) -join ','
        if ($actual -ne $Expected) { throw "$Case expected [$Expected], got [$actual]" }
    }
    $points = @((Point-Row 'before' ($start-1)), (Point-Row 'start' $start),
        (Point-Row 'inside' ($start+500) 'warmup'), (Point-Row 'end' $end),
        (Point-Row 'after' ($end+1)), (Point-Row 'delayed-teardown' ($end+5000)))
    Assert-Ids (Select-MeasurementPoints $points $window) 'start,inside,end' 'point bounds / stale heartbeat'
    $straddling = Point-Row 'query-crosses-start' ($start+1)
    $straddling.sampleQueryStartCounterTicks = $start-1
    Assert-Ids (Select-MeasurementPoints @($straddling) $window) '' 'point query envelope'
    $intervals = @((Cpu-Row 'before' ($start-100) ($start-1)),
        (Cpu-Row 'crosses-start' ($start-1) ($start+1)), (Cpu-Row 'inside' ($start+1) ($end-1)),
        (Cpu-Row 'inclusive' $start $end), (Cpu-Row 'crosses-end' ($end-1) ($end+1)),
        (Cpu-Row 'after' ($end+1) ($end+2)))
    Assert-Ids (Select-MeasurementCpuIntervals $intervals $window) 'inside,inclusive' 'CPU interval bounds'
    $cpuEnvelope = Cpu-Row 'CPU-query-crosses-start' $start ($start+2)
    $cpuEnvelope.cpuWindowStartQueryCounterTicks = $start-1
    Assert-Ids (Select-MeasurementCpuIntervals @($cpuEnvelope) $window) '' 'CPU query envelope'
    $stale = Cpu-Row 'inside-stale-warmup' ($start+1) ($end-1)
    $stale.phase = $stale.cpuWindowStartPhase = 'warmup'
    Assert-Ids (Select-MeasurementCpuIntervals @($stale) $window) 'inside-stale-warmup' 'CPU stale heartbeat'
    $unprimed = Cpu-Row 'unprimed' $start ($start+1); $unprimed.cpuValidity = 'UNPRIMED'
    Assert-Ids (Select-MeasurementCpuIntervals @($unprimed) $window) '' 'unprimed CPU'
    $invalid = Point-Row 'invalid' ($start+1); $invalid.requiredMetricsValidity = 'INVALID'
    Assert-Ids (Select-MeasurementPoints @($invalid) $window) '' 'invalid required metrics'
    $selected = @(Select-MeasurementPoints $points $window)
    $selectedCpu = @(Select-MeasurementCpuIntervals $intervals $window)
    $after = @($points | Where-Object { $_.sampleCounterTicks -gt $end })
    [ordered]@{ result = 'PASS'; checks = @('point before/inside/after', 'inclusive endpoints',
        'point query envelope', 'CPU inside/crossing start/crossing end/after', 'CPU query envelope',
        'stale heartbeat ignored', 'unprimed CPU excluded', 'invalid required metrics excluded',
        'int64 precision', 'missing/incompatible boundary fails closed',
        'clean delayed teardown fixture: post-end points and intervals excluded');
        syntheticWindow = $window; delayedTeardown = @{ syntheticFixture = $true; cleanShutdown = $true;
            teardownCompleteCounterTicks = $end+5500L; staleHeartbeatPhase = 'measurement';
            postMeasurementSampleCount = $after.Count;
            selectedTeardownSamples = @($selected | Where-Object { $_.sampleCounterTicks -gt $end }).Count;
            postEndCpuIntervalCount = @($intervals | Where-Object { $_.cpuSampleCounterTicks -gt $end }).Count;
            selectedPostEndCpuIntervals = @($selectedCpu | Where-Object { $_.cpuSampleCounterTicks -gt $end }).Count } } | ConvertTo-Json -Depth 6
}
if ($SelfTest) { Test-MeasurementSelectors; return }

$frozenHash = '928AC1EA4B3F8329160AFB9AA0BE1F916C01A4A4CC3A6D6A03670AC3837DBD3A'
$repositoryPath = (Resolve-Path -LiteralPath $Repository).Path
function Resolve-UnderRepository([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $repositoryPath $Path))
}
$buildPath = Resolve-UnderRepository $BuildDirectory
$mediaPath = Resolve-UnderRepository $Media
if ($WarmupTimeoutSeconds -lt $WarmupSeconds) { throw 'Warmup timeout is shorter than warmup.' }
# Fixture hash authority is here, before launch, not duplicated in the child.
if (-not (Test-Path -LiteralPath $mediaPath -PathType Leaf)) { throw "Fixture missing: $mediaPath" }
$fixtureHash = (Get-FileHash -LiteralPath $mediaPath -Algorithm SHA256).Hash
if ($fixtureHash -ne $frozenHash) { throw "Fixture hash mismatch; harness NOT launched: $fixtureHash" }
$fixtureBytes = (Get-Item -LiteralPath $mediaPath).Length
if ($fixtureBytes -ne 178528) { throw 'Frozen fixture size mismatch; harness NOT launched.' }
$executable = Join-Path $buildPath 'bin/Release/DesktopSceneEngine_EngineGraphicsSoak.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Release harness missing: $executable" }
$runRoot = Join-Path $buildPath 'soak'
# Only generated evidence inside this repository's build tree; never a cleanup
# target supplied by an unresolved variable or a broad recursive deletion.
if (-not $runRoot.StartsWith($repositoryPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Collector output must remain inside the repository.'
}
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ', $invariant)
$runPath = Join-Path $runRoot $runId
if (Test-Path -LiteralPath $runPath) { throw "Run directory already exists: $runPath" }
$null = New-Item -ItemType Directory -Path $runPath
Write-Host "Run directory: $runPath"

function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 24) + "`n", [Text.UTF8Encoding]::new($false))
}
function Number($Value) {
    if ($null -eq $Value) { return 'NA' }
    if ($Value -is [double] -or $Value -is [single] -or $Value -is [decimal]) { return $Value.ToString('R', $invariant) }
    if ($Value -is [IFormattable]) { return $Value.ToString($null, $invariant) }
    return [string]$Value
}
function Read-Number([string]$Value) {
    if ($Value -eq 'NA' -or [string]::IsNullOrEmpty($Value)) { return $null }
    return [double]::Parse($Value, $invariant)
}
function Read-Heartbeat([string]$Path) {
    # Permit the single writer's replacement/write operations; keep the native
    # file handle shorter-lived than JSON parsing. A race still retries normally.
    $stream = [IO.FileStream]::new($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader = [IO.StreamReader]::new($stream)
    try { $text = $reader.ReadToEnd() }
    finally { $reader.Dispose() }
    return $text | ConvertFrom-Json
}
function Write-CsvRow([IO.StreamWriter]$Writer, $Row) {
    $values = foreach ($value in $Row.Values) { '"' + (Number $value).Replace('"', '""') + '"' }
    $Writer.WriteLine(($values -join ',')); $Writer.Flush()
}
function Collector-Error([string]$Message) {
    [IO.File]::AppendAllText((Join-Path $runPath 'collector-errors.log'),
        [DateTime]::UtcNow.ToString('o', $invariant) + ' ' + $Message + "`n", [Text.UTF8Encoding]::new($false))
}

# Narrow local interop: process resource facts + optional persistent PDH query.
# No helper process, injection, Engine thread, or in-process OS telemetry.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class SoakNative {
    [DllImport("kernel32.dll")] static extern bool QueryPerformanceCounter(out long value);
    [DllImport("kernel32.dll")] static extern bool QueryPerformanceFrequency(out long value);
    public static long CounterNow() {
        long value; if (!QueryPerformanceCounter(out value)) throw new InvalidOperationException("QueryPerformanceCounter failed");
        return value;
    }
    public static long CounterFrequency() {
        long value; if (!QueryPerformanceFrequency(out value) || value <= 0) throw new InvalidOperationException("QueryPerformanceFrequency failed");
        return value;
    }
    [StructLayout(LayoutKind.Sequential)] public struct Memory {
        public uint cb, faults;
        public UIntPtr peakWs, ws, peakPaged, paged, peakNonpaged, nonpaged, pagefile, peakPagefile, privateUsage, privateWs;
        public ulong sharedCommit;
    }
    [StructLayout(LayoutKind.Sequential)] public struct Io {
        public ulong reads, writes, other, readBytes, writeBytes, otherBytes;
    }
    [StructLayout(LayoutKind.Sequential)] public struct Power {
        public byte ac, flag, percent, reserved;
        public uint life, fullLife;
    }
    [DllImport("psapi.dll", SetLastError=true)] public static extern bool GetProcessMemoryInfo(IntPtr process, ref Memory value, uint size);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetProcessIoCounters(IntPtr process, out Io value);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetSystemPowerStatus(out Power value);
    [DllImport("kernel32.dll")] static extern void SetLastError(uint value);
    [DllImport("user32.dll", SetLastError=true)] static extern uint GetGuiResources(IntPtr process, uint flags);
    public static bool Gui(IntPtr process, uint flags, out uint count) {
        SetLastError(0); count = GetGuiResources(process, flags);
        return count != 0 || Marshal.GetLastWin32Error() == 0;
    }
    [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessageW(IntPtr hwnd, uint message, UIntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
}
public sealed class SoakGpu : IDisposable {
    [StructLayout(LayoutKind.Sequential)] struct Value { public uint status; public double number; }
    [StructLayout(LayoutKind.Sequential)] struct Item { public IntPtr name; public Value value; }
    public sealed class Sample { public string Instance; public double Value; }
    [DllImport("pdh.dll", CharSet=CharSet.Unicode)] static extern uint PdhOpenQueryW(string source, UIntPtr user, out IntPtr query);
    [DllImport("pdh.dll", CharSet=CharSet.Unicode)] static extern uint PdhAddEnglishCounterW(IntPtr query, string path, UIntPtr user, out IntPtr counter);
    [DllImport("pdh.dll")] static extern uint PdhCollectQueryData(IntPtr query);
    [DllImport("pdh.dll", CharSet=CharSet.Unicode)] static extern uint PdhGetFormattedCounterArrayW(IntPtr counter, uint format, ref uint bytes, out uint count, IntPtr data);
    [DllImport("pdh.dll")] static extern uint PdhCloseQuery(IntPtr query);
    IntPtr query;
    readonly Dictionary<string, IntPtr> counters = new Dictionary<string, IntPtr>();
    public string Status { get; private set; }
    public SoakGpu() {
        uint result = PdhOpenQueryW(null, UIntPtr.Zero, out query);
        if (result != 0) { Status = "UNAVAILABLE:open:" + result; return; }
        Add("engines", @"\GPU Engine(*)\Utilization Percentage");
        Add("dedicated", @"\GPU Process Memory(*)\Dedicated Usage");
        Add("shared", @"\GPU Process Memory(*)\Shared Usage");
        Status = counters.Count == 3 ? "AVAILABLE" : counters.Count == 0 ? "UNAVAILABLE:counter paths" : "PARTIAL:counter paths";
        PdhCollectQueryData(query); // prime rate counters once; retain the query
    }
    void Add(string name, string path) {
        IntPtr counter; if (PdhAddEnglishCounterW(query, path, UIntPtr.Zero, out counter) == 0) counters.Add(name, counter);
    }
    public Dictionary<string, Sample[]> Read(int pid) {
        var result = new Dictionary<string, Sample[]>();
        if (query == IntPtr.Zero || PdhCollectQueryData(query) != 0) return result;
        string prefix = "pid_" + pid + "_";
        foreach (var pair in counters) {
            uint bytes = 0, count;
            uint status = PdhGetFormattedCounterArrayW(pair.Value, 0x200, ref bytes, out count, IntPtr.Zero);
            if (status != 0x800007D2 || bytes == 0) continue;
            IntPtr buffer = Marshal.AllocHGlobal((int)bytes);
            try {
                status = PdhGetFormattedCounterArrayW(pair.Value, 0x200, ref bytes, out count, buffer);
                if (status != 0) continue;
                var samples = new List<Sample>();
                int size = Marshal.SizeOf<Item>();
                for (int i=0; i<count; ++i) {
                    var item = Marshal.PtrToStructure<Item>(IntPtr.Add(buffer, i*size));
                    string instance = Marshal.PtrToStringUni(item.name);
                    if (instance != null && instance.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)
                        && (item.value.status == 0 || item.value.status == 1) && !double.IsNaN(item.value.number)
                        && !double.IsInfinity(item.value.number)) samples.Add(new Sample { Instance=instance, Value=item.value.number });
                }
                result.Add(pair.Key, samples.ToArray());
            } finally { Marshal.FreeHGlobal(buffer); }
        }
        return result;
    }
    public void Dispose() { if (query != IntPtr.Zero) { PdhCloseQuery(query); query = IntPtr.Zero; } }
}
'@
$counterFrequency = [SoakNative]::CounterFrequency()

function Cim-Metadata([string]$Class, [string[]]$Properties) {
    try { return @(Get-CimInstance -ClassName $Class -ErrorAction Stop | Select-Object -Property $Properties) }
    catch { return @{ validity = 'UNAVAILABLE'; reason = $_.Exception.Message } }
}
function Power-Fact {
    $power = [SoakNative+Power]::new()
    if ([SoakNative]::GetSystemPowerStatus([ref]$power)) {
        return @{ validity = 'VALID'; acLineStatus = $power.ac; batteryPercent = $power.percent }
    }
    return @{ validity = 'UNAVAILABLE' }
}
$collectorProcess = [Diagnostics.Process]::GetCurrentProcess()
$collectorCpuStart = $collectorProcess.TotalProcessorTime.TotalSeconds
$metadata = [ordered]@{
    purpose = 'HARNESS QUALIFICATION / characterization, not canonical performance baseline'
    command = @{ repository = $repositoryPath; build = $buildPath; media = $mediaPath;
        warmupSeconds = $WarmupSeconds; warmupTimeoutSeconds = $WarmupTimeoutSeconds; measureSeconds = $MeasureSeconds;
        framePeriodMs = $FramePeriodMs; sampleSeconds = $SampleSeconds }
    repository = @{ head = (& git -C $repositoryPath rev-parse HEAD); branch = (& git -C $repositoryPath branch --show-current);
        dirtyPaths = @(& git -C $repositoryPath status --short); index = @(& git -C $repositoryPath diff --cached --name-status);
        declaredProtected = @('Sandbox/Application.cpp', 'TestResources/'); harnessUncommitted = $true }
    fixture = @{ path = $mediaPath; bytes = $fixtureBytes; sha256 = $fixtureHash; width = 1920; height = 1080; kind = 'JPEG' }
    executable = @{ path = $executable; sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash; configuration = 'Release' }
    collector = @{ path = $PSCommandPath; sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash;
        pid = $PID; powershell = $PSVersionTable.PSVersion.ToString(); logicalProcessors = [Environment]::ProcessorCount }
    os = (Cim-Metadata 'Win32_OperatingSystem' @('Caption', 'Version', 'BuildNumber', 'OSArchitecture'))
    cpu = (Cim-Metadata 'Win32_Processor' @('Name', 'NumberOfCores', 'NumberOfLogicalProcessors'))
    gpuDrivers = (Cim-Metadata 'Win32_VideoController' @('Name', 'PNPDeviceID', 'DriverVersion', 'DriverDate'))
    power = (Power-Fact)
    buildMetadata = @()
    logging = 'Production logging unchanged; redirected stdout/stderr; bounded harness streams flushed every second; external samples every sampleSeconds'
    nativeEnvironment = 'Two test-owned non-activating HWNDs on primary output; no manual interaction; no Shell deployment'
    metricSemantics = @{ cpu = '100 * delta process CPU seconds / actual monotonic interval; normalized / logical processors';
        privateCommit = 'PrivateMemorySize64 (private bytes); not added to working set';
        gpu = 'busiest PID-filtered engine, never sum parallel engines; memory preserved per instance/adapter LUID';
        processInterval = 'CPU-read endpoint delta in shared QueryPerformanceCounter ticks / frequency; UTC and heartbeat phase are labels only';
        measurementSelection = 'harness-owned inclusive QPC boundaries; complete point-query envelopes and both CPU endpoint-query envelopes contained';
        counterFrequency = $counterFrequency; firstCpuSample = 'UNPRIMED / NA' }
}
$activeScheme = & powercfg /getactivescheme 2>$null
$metadata.power.activeSchemeUuid = if ($activeScheme -match '[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}') { $Matches[0] } else { 'UNAVAILABLE' }
$cachePath = Join-Path $buildPath 'CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath) {
    $metadata.buildMetadata = @(Get-Content -LiteralPath $cachePath | Where-Object {
        $_ -match '^(Qt6_DIR|FFMPEG_libav(codec|format|util)_VERSION|CMAKE_CXX_FLAGS_RELEASE|CMAKE_GENERATOR):' })
}
Write-Json (Join-Path $runPath 'summary.json') ([ordered]@{ result = 'RUNNING'; comparability = 'INVALID'; metadata = $metadata })

$gpu = [SoakGpu]::new()
$samplePath = Join-Path $runPath 'samples.csv'
$sampleWriter = [IO.StreamWriter]::new($samplePath, $false, [Text.UTF8Encoding]::new($false))
$process = $null
$identity = $null
$watch = [Diagnostics.Stopwatch]::new()
$failures = [Collections.Generic.List[string]]::new()
$forced = $false
$graceRequested = $false
$graceAt = 0.0
$lastHeartbeatSequence = -1L
$lastHeartbeatAt = 0.0
$longestHeartbeatGap = 0.0
$heartbeat = $null
$heartbeatReads = 0
$heartbeatTransientReads = 0
$previousCpu = $null
$previousCpuCounter = $null
$previousCpuQueryStart = $null
$collectorCounterOrigin = $null
$previousIo = $null
$previousPhase = 'initialization'
$metricRows = 0
$requiredValidRows = 0
$optionalStatus = [ordered]@{ privateWorkingSet = 'UNAVAILABLE'; io = 'UNAVAILABLE'; gpu = $gpu.Status }

function Owned-Process {
    if ($null -eq $process -or $process.HasExited) { return $false }
    $process.Refresh()
    return $process.StartTime.ToUniversalTime().ToFileTimeUtc() -eq $identity.startFileTimeUtc
}
function Request-OrderlyClose {
    if (-not (Owned-Process)) { return }
    if ($null -ne $heartbeat) {
        foreach ($property in @('compositionHwnd', 'ordinaryHwnd')) {
            if ($heartbeat.PSObject.Properties.Name -contains $property) {
                $window = [IntPtr][long]$heartbeat.$property
                $owner = 0u
                $null = [SoakNative]::GetWindowThreadProcessId($window, [ref]$owner)
                if ($owner -eq $process.Id) { $null = [SoakNative]::PostMessageW($window, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero) }
            }
        }
    }
    else { $null = $process.CloseMainWindow() }
}
function Collect-Sample([double]$Elapsed) {
    if (-not (Owned-Process)) { return }
    $sampleQueryStart = [SoakNative]::CounterNow()
    $process.Refresh()
    $phase = if ($null -ne $heartbeat) { [string]$heartbeat.phase } else { 'initialization' }
    $valid = $true
    $cpuSeconds = $ws = $private = $handles = $threads = $gdi = $user = $null
    $cpuQueryStart = $cpuCounter = $null
    try {
        $cpuQueryStart = [SoakNative]::CounterNow()
        $cpuSeconds = $process.TotalProcessorTime.TotalSeconds
        $cpuCounter = [SoakNative]::CounterNow()
        $ws = $process.WorkingSet64; $private = $process.PrivateMemorySize64
        $handles = $process.HandleCount; $threads = $process.Threads.Count
        $gdiCount = 0u; $userCount = 0u
        if (-not [SoakNative]::Gui($process.Handle, 0, [ref]$gdiCount)) { throw 'GDI resource query failed' }
        if (-not [SoakNative]::Gui($process.Handle, 1, [ref]$userCount)) { throw 'USER resource query failed' }
        $gdi = $gdiCount; $user = $userCount
    }
    catch { $valid = $false; Collector-Error "Required process metric: $($_.Exception.Message)" }
    $interval = if ($null -ne $cpuCounter -and $null -ne $script:previousCpuCounter) {
        ($cpuCounter - $script:previousCpuCounter) / [double]$counterFrequency
    } else { $null }
    $cpuCore = if ($null -ne $cpuSeconds -and $null -ne $script:previousCpu -and $interval -gt 0) {
        100.0 * ($cpuSeconds - $script:previousCpu) / $interval
    } else { $null }
    $cpuValidity = if ($null -eq $cpuCore) { 'UNPRIMED' } elseif ($cpuCore -lt 0) { $valid = $false; 'INVALID' } else { 'VALID' }
    $memory = [SoakNative+Memory]::new()
    $memory.cb = [Runtime.InteropServices.Marshal]::SizeOf($memory)
    $privateWs = $null
    $privateWsValidity = 'UNAVAILABLE'
    if ([SoakNative]::GetProcessMemoryInfo($process.Handle, [ref]$memory, $memory.cb)) {
        $privateWs = $memory.privateWs.ToUInt64(); $privateWsValidity = 'VALID'; $script:optionalStatus.privateWorkingSet = 'AVAILABLE'
    }
    $io = [SoakNative+Io]::new(); $ioValidity = 'UNAVAILABLE'; $readRate = $writeRate = $null
    if ([SoakNative]::GetProcessIoCounters($process.Handle, [ref]$io)) {
        $ioValidity = 'VALID'; $script:optionalStatus.io = 'AVAILABLE'
        if ($null -ne $script:previousIo -and $interval -gt 0) {
            $readRate = ($io.readBytes - $script:previousIo.readBytes) / $interval
            $writeRate = ($io.writeBytes - $script:previousIo.writeBytes) / $interval
        }
    }
    $gpuValues = $gpu.Read($process.Id)
    $engineValid = $gpuValues.ContainsKey('engines') -and $gpuValues['engines'].Length -gt 0
    $memoryValid = $gpuValues.ContainsKey('dedicated') -and $gpuValues.ContainsKey('shared') `
        -and $gpuValues['dedicated'].Length -gt 0 -and $gpuValues['shared'].Length -gt 0
    $busiest = if ($engineValid) { $gpuValues['engines'] | Sort-Object -Property Value -Descending | Select-Object -First 1 } else { $null }
    $memoryRows = @()
    foreach ($kind in @('dedicated', 'shared')) {
        if ($gpuValues.ContainsKey($kind)) {
            foreach ($value in $gpuValues[$kind]) {
                $luid = if ($value.Instance -match '(luid_0x[0-9a-f]+_0x[0-9a-f]+)') { $Matches[1] } else { 'NA' }
                $memoryRows += @{ kind = $kind; instance = $value.Instance; adapterLuid = $luid; bytes = $value.Value }
            }
        }
    }
    $gpuValidity = if ($engineValid -and $memoryValid) { 'AVAILABLE' } elseif ($engineValid -or $memoryValid) { 'PARTIAL' } else { 'UNAVAILABLE:no valid exact-PID instance' }
    if ($gpuValidity -eq 'AVAILABLE' -or ($gpuValidity -eq 'PARTIAL' -and $script:optionalStatus.gpu -ne 'AVAILABLE')) { $script:optionalStatus.gpu = $gpuValidity }
    $power = Power-Fact
    $sampleCounter = [SoakNative]::CounterNow()
    $row = [ordered]@{
        utc = [DateTime]::UtcNow.ToString('o', $invariant);
        elapsedSeconds = ($sampleCounter - $collectorCounterOrigin) / [double]$counterFrequency;
        collectorPollElapsedSeconds = $Elapsed; pid = $process.Id;
        sampleQueryStartCounterTicks = $sampleQueryStart; sampleCounterTicks = $sampleCounter;
        cpuQueryStartCounterTicks = $cpuQueryStart; cpuSampleCounterTicks = $cpuCounter;
        cpuWindowStartQueryCounterTicks = $script:previousCpuQueryStart; cpuWindowStartCounterTicks = $script:previousCpuCounter;
        processStartUtc = $identity.startUtc; phase = $phase; cpuWindowStartPhase = $script:previousPhase;
        heartbeatSequence = if ($null -ne $heartbeat) { $heartbeat.sequence } else { $null };
        heartbeatElapsedSeconds = if ($null -ne $heartbeat) { $heartbeat.elapsedSeconds } else { $null };
        actualIntervalSeconds = $interval; cpuSeconds = $cpuSeconds; cpuCoreEquivalentPercent = $cpuCore;
        cpuNormalizedPercent = if ($null -ne $cpuCore) { $cpuCore / [Environment]::ProcessorCount } else { $null };
        cpuValidity = $cpuValidity; workingSetBytes = $ws; privateBytes = $private;
        privateCommitBytes = $private; handleCount = $handles; threadCount = $threads; gdiCount = $gdi; userCount = $user;
        requiredMetricsValidity = if ($valid) { 'VALID' } else { 'INVALID' };
        privateWorkingSetBytes = $privateWs; privateWorkingSetValidity = $privateWsValidity;
        readBytes = if ($ioValidity -eq 'VALID') { $io.readBytes } else { $null };
        writeBytes = if ($ioValidity -eq 'VALID') { $io.writeBytes } else { $null };
        otherBytes = if ($ioValidity -eq 'VALID') { $io.otherBytes } else { $null };
        readOperations = if ($ioValidity -eq 'VALID') { $io.reads } else { $null };
        writeOperations = if ($ioValidity -eq 'VALID') { $io.writes } else { $null };
        otherOperations = if ($ioValidity -eq 'VALID') { $io.other } else { $null };
        readBytesPerSecond = $readRate; writeBytesPerSecond = $writeRate; ioValidity = $ioValidity;
        gpuBusiestEnginePercent = if ($null -ne $busiest) { $busiest.Value } else { $null };
        gpuBusiestEngineInstance = if ($null -ne $busiest) { $busiest.Instance } else { 'NA' };
        gpuMemoryByInstance = ConvertTo-Json -InputObject $memoryRows -Compress -Depth 4; gpuValidity = $gpuValidity;
        acLineStatus = if ($power.validity -eq 'VALID') { $power.acLineStatus } else { $null }; powerValidity = $power.validity
    }
    if ($script:metricRows -eq 0) { $sampleWriter.WriteLine(($row.Keys -join ',')) }
    Write-CsvRow $sampleWriter $row
    ++$script:metricRows
    if ($valid) { ++$script:requiredValidRows }
    else { $failures.Add('Required process metrics unavailable') }
    $script:previousCpu = $cpuSeconds; $script:previousCpuCounter = $cpuCounter;
    $script:previousCpuQueryStart = $cpuQueryStart; $script:previousPhase = $phase
    $script:previousIo = if ($ioValidity -eq 'VALID') { $io } else { $null }
}

try {
    $argumentText = '--media "' + $mediaPath + '" --output-dir "' + $runPath + '"' +
        ' --warmup-seconds ' + (Number $WarmupSeconds) + ' --warmup-timeout-seconds ' + (Number $WarmupTimeoutSeconds) +
        ' --measure-seconds ' + (Number $MeasureSeconds) + ' --frame-period-ms ' + (Number $FramePeriodMs)
    $collectorCounterOrigin = [SoakNative]::CounterNow()
    $metadata.metricSemantics.collectorCounterOriginTicks = $collectorCounterOrigin
    $watch.Start()
    $process = Start-Process -FilePath $executable -ArgumentList $argumentText -WorkingDirectory $repositoryPath `
        -WindowStyle Hidden -RedirectStandardOutput (Join-Path $runPath 'stdout.log') `
        -RedirectStandardError (Join-Path $runPath 'stderr.log') -PassThru
    $identity = @{ pid = $process.Id; startUtc = $process.StartTime.ToUniversalTime().ToString('o', $invariant);
        startFileTimeUtc = $process.StartTime.ToUniversalTime().ToFileTimeUtc() }
    $metadata.process = $identity
    Write-Host "Owned harness PID: $($process.Id); requested measurement: $MeasureSeconds seconds"
    $nextSample = 0.0
    $heartbeatPath = Join-Path $runPath 'heartbeat.json'
    $deadline = $WarmupTimeoutSeconds + $MeasureSeconds + 30.0
    while (-not $process.HasExited) {
        $elapsed = $watch.Elapsed.TotalSeconds
        if (Test-Path -LiteralPath $heartbeatPath) {
            try {
                $candidate = Read-Heartbeat $heartbeatPath
                ++$heartbeatReads
                if ($candidate.sequence -gt $lastHeartbeatSequence) {
                    $gap = $elapsed - $lastHeartbeatAt
                    $longestHeartbeatGap = [Math]::Max($longestHeartbeatGap, $gap)
                    $lastHeartbeatSequence = [long]$candidate.sequence; $lastHeartbeatAt = $elapsed
                }
                $heartbeat = $candidate
            }
            catch { ++$heartbeatTransientReads } # transient replace/partial read; stale timer remains authoritative
        }
        $heartbeatAge = $elapsed - $lastHeartbeatAt
        if (-not $graceRequested -and ($heartbeatAge -gt 30 -or $elapsed -gt $deadline)) {
            $reason = if ($heartbeatAge -gt 30) { 'Stale/missing heartbeat >30 seconds' } else { 'Bounded run timeout' }
            $failures.Add($reason); Collector-Error $reason
            # Capture remaining observable process facts before requesting close.
            Collect-Sample $elapsed
            Request-OrderlyClose
            $graceRequested = $true; $graceAt = $elapsed
        }
        if ($elapsed -ge $nextSample -and (Owned-Process)) {
            Collect-Sample $elapsed
            $nextSample = ([Math]::Floor($elapsed / $SampleSeconds) + 1) * $SampleSeconds
        }
        if ($graceRequested -and $elapsed - $graceAt -gt 30) {
            if (Owned-Process) {
                Collect-Sample $elapsed
                Collector-Error 'Forcing exact PID after evidence capture and 30-second orderly-close grace.'
                $process.Kill(); $forced = $true; $failures.Add('Forced termination / invalid lifecycle')
            }
            break
        }
        $null = $process.WaitForExit(200) # bounded wait, heartbeat/process watch at <=200-ms cadence
    }
    $process.WaitForExit(5000) | Out-Null
}
catch {
    $failures.Add('Collector failure: ' + $_.Exception.Message)
    Collector-Error $_.Exception.ToString()
    # An apparatus failure must not strand its own child. No unrelated PID is touched.
    if (Owned-Process) {
        Request-OrderlyClose
        if (-not $process.WaitForExit(30000) -and (Owned-Process)) {
            Collector-Error 'Forcing exact PID after collector failure and 30-second close grace.'
            $process.Kill(); $forced = $true; $null = $process.WaitForExit(5000)
        }
    }
}
finally { $sampleWriter.Dispose(); $gpu.Dispose(); $watch.Stop() }

# Post-exit analysis only: no growing sample history inside the measured Engine
# or the live collector. Numeric frame columns are retained for exact quantiles.
function Statistics($Values) {
    $sorted = @($Values | Where-Object { $null -ne $_ } | Sort-Object)
    if ($sorted.Count -eq 0) { return @{ validity = 'UNAVAILABLE'; count = 0 } }
    $count = $sorted.Count
    $median = if ($count % 2) { $sorted[[int][Math]::Floor($count / 2)] } else { ($sorted[$count/2-1] + $sorted[$count/2]) / 2.0 }
    return [ordered]@{ validity = 'VALID'; count = $count; mean = ($sorted | Measure-Object -Average).Average; median = $median;
        p95 = $sorted[[Math]::Max(0, [int][Math]::Ceiling(.95*$count)-1)];
        p99 = $sorted[[Math]::Max(0, [int][Math]::Ceiling(.99*$count)-1)]; max = $sorted[-1]; min = $sorted[0] }
}
function Memory-Statistics($Rows, [string]$Field) {
    if ($Rows.Count -lt 2) { return @{ validity = 'INSUFFICIENT_SAMPLES'; sampleCount = $Rows.Count } }
    $start = Read-Number $Rows[0].elapsedSeconds; $end = Read-Number $Rows[-1].elapsedSeconds
    $windowLimit = if ($end-$start -ge 1200) { 300.0 } else { 30.0 }
    $window = [Math]::Min($windowLimit, [Math]::Max($SampleSeconds, ($end-$start)/4))
    $startValues = @($Rows | Where-Object { (Read-Number $_.elapsedSeconds) -le $start+$window } | ForEach-Object { Read-Number $_.$Field })
    $endValues = @($Rows | Where-Object { (Read-Number $_.elapsedSeconds) -ge $end-$window } | ForEach-Object { Read-Number $_.$Field })
    $allValues = @($Rows | ForEach-Object { Read-Number $_.$Field })
    $meanX = ($Rows | ForEach-Object { ((Read-Number $_.elapsedSeconds)-$start)/60 } | Measure-Object -Average).Average
    $meanY = ($allValues | Measure-Object -Average).Average
    $numerator = 0.0; $denominator = 0.0
    foreach ($row in $Rows) {
        $x = ((Read-Number $row.elapsedSeconds)-$start)/60 - $meanX
        $numerator += $x*((Read-Number $row.$Field)-$meanY); $denominator += $x*$x
    }
    return [ordered]@{ validity = 'VALID'; sampleCount = $Rows.Count; windowSeconds = $window;
        windowDefinition = if ($windowLimit -eq 300) { 'first/last up-to-five-minute measurement windows' }
            else { 'short-run first/last <=30-second measurement windows, not five-minute baseline windows' };
        startWindowMedianBytes = (Statistics $startValues).median; endWindowMedianBytes = (Statistics $endValues).median;
        slopeBytesPerMinute = if ($denominator -gt 0) { $numerator/$denominator } else { $null };
        highWaterBytes = ($allValues | Measure-Object -Maximum).Maximum; method = 'OLS bytes vs actual monotonic minutes, warmup excluded; positive slope is not a leak verdict' }
}
$harness = $null; $events = @(); $measurementRows = @(); $cpuRows = @(); $measurementWindow = $null
$frameStatistics = [ordered]@{}
$harnessPath = Join-Path $runPath 'harness-result.json'
try {
    if (-not (Test-Path -LiteralPath $harnessPath)) { throw 'Terminal harness result missing' }
    $harness = Get-Content -LiteralPath $harnessPath -Raw | ConvertFrom-Json
    $measurementWindow = Get-MeasurementWindow $harness.measurementWindow $counterFrequency
    $events = @(Get-Content -LiteralPath (Join-Path $runPath 'harness-events.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
    $metadata.displaySnapshot = ($events | Where-Object event -eq 'start' | Select-Object -First 1).facts.displaySnapshot
    $metadata.windows = ($events | Where-Object event -eq 'windows_created' | Select-Object -First 1).facts
    $metadata.metricSemantics.totalWorkMs = 'actual frame start through update/render/diagnostics/row preparation/events/heartbeat; final timing scalar/newline/CSV flush excluded, but present in subsequent actual frame intervals'
    $samples = @(Import-Csv -LiteralPath $samplePath)
    $measurementRows = @(Select-MeasurementPoints $samples $measurementWindow)
    $cpuRows = @(Select-MeasurementCpuIntervals $samples $measurementWindow)
    if ($measurementRows.Count -lt 2) { $failures.Add('Insufficient measurement-phase required process samples') }
    if ($cpuRows.Count -lt 2) { $failures.Add('Insufficient fully contained measurement CPU intervals') }
    $startEvent = @($events | Where-Object event -eq 'measurement_start')
    $endEvent = @($events | Where-Object event -eq 'measurement_end')
    if ($startEvent.Count -ne 1 -or $endEvent.Count -ne 1 `
        -or [long]$startEvent[0].facts.boundary.counterTicks -ne $measurementWindow.startCounterTicks `
        -or [long]$endEvent[0].facts.boundary.counterTicks -ne $measurementWindow.endCounterTicks) {
        throw 'Authoritative boundary events and terminal window disagree'
    }
    $columns = @('frameIntervalMs', 'updateMs', 'renderMs', 'totalWorkMs', 'latenessMs')
    $numbers = @{}; foreach ($column in $columns) { $numbers[$column] = [Collections.Generic.List[double]]::new() }
    $reader = [IO.StreamReader]::new((Join-Path $runPath 'frames.csv'))
    $measurementFrameCount = 0L; $observedPhaseFrames = 0L
    try {
        $headers = $reader.ReadLine().Split(','); $indexes = @{}
        for ($i=0; $i -lt $headers.Length; ++$i) { $indexes[$headers[$i]] = $i }
        $expectedSequence = 1L
        while ($null -ne ($line = $reader.ReadLine())) {
            $fields = $line.Split(',')
            if ($fields.Length -ne $headers.Length) { throw 'Frame CSV field count mismatch' }
            if ([long]$fields[$indexes.sequence] -ne $expectedSequence) { throw 'Frame CSV sequence discontinuity' }
            ++$expectedSequence; ++$observedPhaseFrames
            if ($fields[$indexes.phase] -ne 'measurement') { continue }
            ++$measurementFrameCount
            foreach ($column in $columns) { $numbers[$column].Add((Read-Number $fields[$indexes[$column]])) }
        }
    }
    finally { $reader.Dispose() }
    foreach ($column in $columns) { $frameStatistics[$column] = Statistics $numbers[$column] }
    if ($measurementFrameCount -ne $harness.measurementFrames -or $observedPhaseFrames -ne ($harness.measurementFrames+$harness.warmupFrames)) {
        $failures.Add('Streamed frame counts do not match terminal result')
    }
    if (-not $harness.cleanShutdown) { $failures.Add('Harness shutdown not clean') }
    if ($harness.measurementElapsedSeconds -lt $MeasureSeconds) { $failures.Add('Intended measurement duration not completed') }
    if ($harness.result -eq 'FAIL') { $failures.Add('Harness failure: ' + $harness.reason) }
    $eventNames = @($events.event)
    $detach = [Array]::IndexOf($eventNames, 'ordinary_detached'); $release = [Array]::IndexOf($eventNames, 'engine_shutdown')
    $destroy = [Array]::IndexOf($eventNames, 'windows_destroyed')
    if ([Array]::IndexOf($eventNames, 'composition_detached') -lt 0 -or $detach -lt 0 -or $release -le $detach -or $destroy -le $release) {
        $failures.Add('Detach/Engine-release/HWND-destruction event order invalid')
    }
}
catch { $failures.Add('Output validation: ' + $_.Exception.Message); Collector-Error $_.Exception.ToString() }
if ($null -eq $process -or -not $process.HasExited) { $failures.Add('Owned process did not exit') }
elseif ($process.ExitCode -ne 0) { $failures.Add('Owned process exit code: ' + $process.ExitCode) }
if ($metricRows -eq 0 -or $requiredValidRows -ne $metricRows) { $failures.Add('Required process metric capture incomplete') }
if ($lastHeartbeatSequence -lt 1) { $failures.Add('Live heartbeat never observed') }
$uniqueFailures = @($failures | Select-Object -Unique)
$result = if ($uniqueFailures.Count) { 'FAIL' } elseif ($harness.result -eq 'INCONCLUSIVE') { 'INCONCLUSIVE' } else { 'PASS' }
$comparability = if ($result -eq 'FAIL') { 'INVALID' } elseif ($result -eq 'INCONCLUSIVE') { 'PARTIAL' } else { 'QUALIFIED' }
$resourceCounts = [ordered]@{}
foreach ($field in @('handleCount', 'threadCount', 'gdiCount', 'userCount')) {
    if ($measurementRows.Count) {
        $resourceCounts[$field] = @{ start = Read-Number $measurementRows[0].$field; end = Read-Number $measurementRows[-1].$field;
            max = ($measurementRows | ForEach-Object { Read-Number $_.$field } | Measure-Object -Maximum).Maximum }
    }
}
$collectorProcess.Refresh()
$collectorCpuSeconds = $collectorProcess.TotalProcessorTime.TotalSeconds - $collectorCpuStart
$evidenceFiles = @(Get-ChildItem -LiteralPath $runPath -File)
$evidenceBytes = ($evidenceFiles | Measure-Object -Property Length -Sum).Sum
$summary = [ordered]@{
    result = $result; comparability = $comparability; failures = $uniqueFailures; metadata = $metadata;
    measurementSelection = @{ window = $measurementWindow; pointSamples = $measurementRows.Count; cpuIntervals = $cpuRows.Count;
        firstPointCounterTicks = if ($measurementRows.Count) { [long]$measurementRows[0].sampleCounterTicks } else { $null };
        lastPointCounterTicks = if ($measurementRows.Count) { [long]$measurementRows[-1].sampleCounterTicks } else { $null };
        firstCpuIntervalStartCounterTicks = if ($cpuRows.Count) { [long]$cpuRows[0].cpuWindowStartCounterTicks } else { $null };
        lastCpuIntervalEndCounterTicks = if ($cpuRows.Count) { [long]$cpuRows[-1].cpuSampleCounterTicks } else { $null } };
    harness = $harness; collector = @{ totalElapsedSeconds = $watch.Elapsed.TotalSeconds; metricRows = $metricRows;
        requiredValidRows = $requiredValidRows; measurementMetricRows = $measurementRows.Count;
        heartbeatReads = $heartbeatReads; transientHeartbeatReads = $heartbeatTransientReads; longestObservedHeartbeatGapSeconds = $longestHeartbeatGap;
        forcedTermination = $forced; orderlyCloseRequested = $graceRequested; optionalMetricCoverage = $optionalStatus;
        cpuSecondsIncludingMetadataAndPostprocessing = $collectorCpuSeconds;
        cpuCoreEquivalentPercentDuringRunApprox = if ($watch.Elapsed.TotalSeconds -gt 0) { 100*$collectorCpuSeconds/$watch.Elapsed.TotalSeconds } else { $null };
        overheadNote = 'Collector CPU includes metadata and post-exit analysis; ratio is a conservative apparatus estimate, not Engine CPU subtraction' };
    statistics = @{ percentileDefinition = 'median averages central pair; p95/p99 nearest-rank ceiling(p*N); measurement only';
        cpuCoreEquivalentPercent = Statistics @($cpuRows | ForEach-Object { Read-Number $_.cpuCoreEquivalentPercent });
        cpuNormalizedPercent = Statistics @($cpuRows | ForEach-Object { Read-Number $_.cpuNormalizedPercent });
        workingSet = Memory-Statistics $measurementRows 'workingSetBytes'; privateBytes = Memory-Statistics $measurementRows 'privateBytes';
        resources = $resourceCounts; frames = $frameStatistics };
    output = @{ directory = $runPath; bytesBeforeFinalSummary = $evidenceBytes;
        files = @($evidenceFiles | Select-Object Name, Length);
        streamingRateBytesPerSecond = if ($watch.Elapsed.TotalSeconds -gt 0) { $evidenceBytes/$watch.Elapsed.TotalSeconds } else { $null };
        ownership = 'child: frames.csv,harness-events.jsonl,heartbeat.json,harness-result.json; collector: samples.csv,summary.json,summary.txt,stdout.log,stderr.log,collector-errors.log (only on error)' };
    limitations = @('Not Player', 'Not video/decoder/audio/BGM throughput', 'Not physical Desktop/Shell',
        'Draws/Present attempts are not displayed FPS, successful Present counts, or GPU scan-out latency',
        'Optional missing metrics are NA, never zero; GPU process memory is adapter/instance-specific',
        'Two-minute characterization is not a performance regression or memory leak verdict');
    canonicalBaseline = 'NOT RUN; separately authorized, frozen/reviewed apparatus required'
}
Write-Json (Join-Path $runPath 'summary.json') $summary
$text = "$result / $comparability`n" + ($summary | ConvertTo-Json -Depth 24) + "`n"
[IO.File]::WriteAllText((Join-Path $runPath 'summary.txt'), $text, [Text.UTF8Encoding]::new($false))
Write-Host "$result / $comparability; required metric rows $requiredValidRows/$metricRows; measurement samples $($measurementRows.Count)"
Write-Host "Summary: $(Join-Path $runPath 'summary.json')"
if ($result -eq 'FAIL') { exit 1 }
