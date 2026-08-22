<#
.SYNOPSIS
    Collects coupling / duplication metrics for the DirectX12 foundation layer.

.DESCRIPTION
    Baseline + regression tool for the DX12 foundation refactoring
    (see Docs/Engine/Graphics/Common/DX12_Foundation_Refactoring_Review.md).

    Re-run this after every phase and diff the generated snapshot against the
    previous one. Numbers are expected to go DOWN; a number going up means the
    refactoring leaked more coupling than it removed.

    The script auto-detects the foundation layer directory, so it keeps working
    after the planned Graphics/Common -> Graphics/RHI move, and it counts both
    the old (DirectXCommon) and the new (GraphicsCore) facade names.

.PARAMETER Root
    Repository root (the folder that contains Engine\ and Application\).
    Defaults to four levels above this script.

.PARAMETER OutFile
    Markdown snapshot path. Defaults to
    Docs\Engine\Graphics\Common\Metrics\Metrics_<yyyy-MM-dd_HHmm>.md

.PARAMETER NoFile
    Print to console only, do not write a snapshot file.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -NoProfile -File Build\Scripts\Collect-DX12FoundationMetrics.ps1
#>
[CmdletBinding()]
param(
    [string]$Root,
    [string]$OutFile,
    [switch]$NoFile
)

$ErrorActionPreference = 'Stop'

# ------------------------------------------------------------------
# Setup
# ------------------------------------------------------------------
if (-not $Root) {
    if ($PSScriptRoot) { $Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path }
    else               { $Root = (Get-Location).Path }
}
if (-not (Test-Path (Join-Path $Root 'Engine\Src'))) {
    throw "Engine\Src not found under '$Root'. Pass -Root <repo root>."
}

$srcRoots = @(
    (Join-Path $Root 'Engine\Src'),
    (Join-Path $Root 'Application\Src')
) | Where-Object { Test-Path $_ }

# Foundation layer: Graphics\RHI after the move, Graphics\Common before it.
$layerCandidates = @('Engine\Src\Graphics\RHI', 'Engine\Src\Graphics\Common')
$layerDir = $null
foreach ($c in $layerCandidates) {
    $p = Join-Path $Root $c
    if (Test-Path $p) { $layerDir = $p; $layerRel = $c; break }
}
if (-not $layerDir) { throw "Foundation layer directory not found (looked for: $($layerCandidates -join ', '))." }

Write-Host "Repo root  : $Root"
Write-Host "Layer dir  : $layerRel"
Write-Host "Loading source files..." -NoNewline

# ------------------------------------------------------------------
# Load every translation unit once (795 files / ~5 MB today)
# ------------------------------------------------------------------
$files = @()
foreach ($r in $srcRoots) {
    Get-ChildItem -Path $r -Recurse -File -Include *.h, *.hpp, *.cpp | ForEach-Object {
        $rel = $_.FullName.Substring($Root.Length).TrimStart('\')
        $raw = [System.IO.File]::ReadAllText($_.FullName)
        # Call-count metrics run against comment-stripped source: a comment that
        # warns "do not call GetCurrentBackBufferIndex() here" must not be counted
        # as a call. Include analysis still uses the raw text.
        $code = [regex]::Replace($raw, '/\*[\s\S]*?\*/', '')
        $code = [regex]::Replace($code, '(?m)//.*$', '')
        $files += [pscustomobject]@{
            Rel     = $rel
            Name    = $_.Name
            Ext     = $_.Extension
            InLayer = $rel.StartsWith($layerRel, [StringComparison]::OrdinalIgnoreCase)
            Text    = $code
            Raw     = $raw
        }
    }
}
Write-Host " $($files.Count) files."

$outside = $files | Where-Object { -not $_.InLayer }
$inLayer = $files | Where-Object { $_.InLayer }

# ------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------
function Measure-Pattern {
    param(
        [Parameter(Mandatory)] [string]   $Pattern,
        [Parameter(Mandatory)] [object[]] $Set,
        [string] $ExtFilter   # '.h' / '.cpp' / $null for both
    )
    $rx    = [regex]::new($Pattern)
    $calls = 0
    $hits  = @()
    foreach ($f in $Set) {
        if ($ExtFilter -and $f.Ext -ne $ExtFilter) { continue }
        $m = $rx.Matches($f.Text)
        if ($m.Count -gt 0) { $calls += $m.Count; $hits += $f.Rel }
    }
    [pscustomobject]@{ Calls = $calls; Files = $hits.Count; Paths = $hits }
}

# Receiver expressions that denote the facade object (old and new names).
$facadeRecv = '(?<![A-Za-z0-9_])(?:dxCommon_|dxCommon|directXCommon_|directXCommon|dxc|dx_|dx|graphicsCore_|graphicsCore|GetDirectXCommon\(\)|GetGraphicsCore\(\))'

function Measure-FacadeAccessor {
    param([Parameter(Mandatory)][string]$Method)
    Measure-Pattern -Pattern ($facadeRecv + '\s*(?:->|\.)\s*' + [regex]::Escape($Method) + '\s*\(') -Set $outside
}

function Measure-MemberCall {
    param([Parameter(Mandatory)][string]$Method, [object[]]$Set = $outside)
    Measure-Pattern -Pattern ('(?:->|\.)\s*' + [regex]::Escape($Method) + '\s*\(') -Set $Set
}

$lines = New-Object System.Collections.Generic.List[string]
function Emit { param([string]$s = '') ; $lines.Add($s) | Out-Null ; Write-Host $s }

# Headline numbers, filled in as each section is computed, rendered as a
# summary table at the top of the snapshot (easy phase-over-phase diffing).
$key = [ordered]@{}

# ------------------------------------------------------------------
# Report
# ------------------------------------------------------------------
$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm'
$head  = (& git -C $Root rev-parse --short HEAD 2>$null)
$brnch = (& git -C $Root rev-parse --abbrev-ref HEAD 2>$null)

Emit "# DX12 Foundation Metrics"
Emit ""
Emit "- Collected : $stamp"
Emit "- Commit    : $brnch @ $head"
Emit "- Layer dir : ``$layerRel``"
Emit "- Scanned   : $($files.Count) files (.h/.hpp/.cpp) under Engine\Src + Application\Src"
Emit ""
Emit "Generated by ``Build\Scripts\Collect-DX12FoundationMetrics.ps1``."
Emit "Re-run after each phase; every number below is expected to shrink."
Emit ""
$summaryAnchor = $lines.Count

# --- 1. Layer inventory ------------------------------------------------------
$layerLines = 0
foreach ($f in $inLayer) { $layerLines += ($f.Raw -split "`n").Count }
Emit "## 1. Layer inventory"
Emit ""
Emit "| Metric | Value |"
Emit "|---|---:|"
Emit "| Files in layer | $($inLayer.Count) |"
Emit "| Lines in layer | $layerLines |"
Emit ""
$key['Files in foundation layer'] = $inLayer.Count
$key['Lines in foundation layer'] = $layerLines

# --- 2. Facade fan-in --------------------------------------------------------
Emit "## 2. Facade fan-in (files that include each layer header)"
Emit ""
Emit "| Header | Including files (outside layer) |"
Emit "|---|---:|"
$headerRows = @()
foreach ($h in ($inLayer | Where-Object { $_.Ext -eq '.h' } | Sort-Object Name)) {
    $pat = '#\s*include\s*[<"][^">]*' + [regex]::Escape($h.Name) + '[">]'
    $r = Measure-Pattern -Pattern $pat -Set $outside
    $headerRows += [pscustomobject]@{ Name = $h.Name; Files = $r.Files }
}
foreach ($row in ($headerRows | Sort-Object -Property Files -Descending)) {
    Emit ("| ``{0}`` | {1} |" -f $row.Name, $row.Files)
}
Emit ""
$facadeHeader = $headerRows | Where-Object { $_.Name -in @('DirectXCommon.h', 'GraphicsCore.h') }
$key['Files including the facade header'] = ($facadeHeader | Measure-Object -Property Files -Sum).Sum

# --- 2b. Rebuild fan-out ------------------------------------------------------
# How many translation units must recompile when a layer header is edited.
# This is what header dieting actually buys: the facade stops dragging the
# manager headers into ~100 unrelated TUs.
Emit "### 2b. Rebuild fan-out (TUs recompiled when the header is touched)"
Emit ""
Emit "Transitive closure over ``#include \"...\"`` edges. Lower is better."
Emit ""

# index every file by the include spellings that can resolve to it
$byPath = @{}
foreach ($f in $files) {
    $norm = $f.Rel -replace '\\', '/'
    foreach ($root in @('Engine/Src/', 'Application/Src/')) {
        if ($norm.StartsWith($root)) { $byPath[$norm.Substring($root.Length)] = $f.Rel }
    }
}
# direct include edges: file -> included files
$directIncludes = @{}
foreach ($f in $files) {
    $deps = New-Object System.Collections.Generic.HashSet[string]
    foreach ($m in [regex]::Matches($f.Text, '#\s*include\s*"([^"]+)"')) {
        $inc = $m.Groups[1].Value -replace '\\', '/'
        if ($byPath.ContainsKey($inc)) { [void]$deps.Add($byPath[$inc]); continue }
        # bare filename: resolve relative to the including file's directory
        $dir = Split-Path $f.Rel -Parent
        $sibling = ((Join-Path $dir $inc) -replace '\\', '/')
        $siblingKey = $sibling -replace '^(Engine|Application)/Src/', ''
        if ($byPath.ContainsKey($siblingKey)) { [void]$deps.Add($byPath[$siblingKey]) }
    }
    $directIncludes[$f.Rel] = $deps
}
# reverse-BFS from each layer header to count reaching TUs (.cpp only)
$reverse = @{}
foreach ($f in $files) {
    foreach ($d in $directIncludes[$f.Rel]) {
        if (-not $reverse.ContainsKey($d)) { $reverse[$d] = New-Object System.Collections.Generic.List[string] }
        $reverse[$d].Add($f.Rel)
    }
}
function Get-FanOut {
    param([string]$Target)
    $seen = New-Object System.Collections.Generic.HashSet[string]
    $queue = New-Object System.Collections.Generic.Queue[string]
    $queue.Enqueue($Target)
    [void]$seen.Add($Target)
    while ($queue.Count -gt 0) {
        $cur = $queue.Dequeue()
        if (-not $reverse.ContainsKey($cur)) { continue }
        foreach ($p in $reverse[$cur]) { if ($seen.Add($p)) { $queue.Enqueue($p) } }
    }
    ($seen | Where-Object { $_ -like '*.cpp' }).Count
}
Emit "| Layer header | TUs recompiled |"
Emit "|---|---:|"
$fanRows = @()
foreach ($h in ($inLayer | Where-Object { $_.Ext -eq '.h' })) {
    $fanRows += [pscustomobject]@{ Name = $h.Name; Rel = $h.Rel; Fan = (Get-FanOut -Target $h.Rel) }
}
foreach ($row in ($fanRows | Sort-Object -Property Fan -Descending)) {
    Emit ("| ``{0}`` | {1} |" -f $row.Name, $row.Fan)
}
Emit ""
$facadeFan = ($fanRows | Where-Object { $_.Name -in @('DirectXCommon.h', 'GraphicsCore.h') } | Measure-Object -Property Fan -Maximum).Maximum
$mgrFan = ($fanRows | Where-Object { $_.Name -in @('DescriptorManager.h', 'CommandManager.h', 'UploadContext.h', 'DeviceManager.h', 'SwapChainManager.h', 'DepthStencilManager.h') } | Measure-Object -Property Fan -Sum).Sum
$key['Rebuild fan-out: facade header'] = $facadeFan
$key['Rebuild fan-out: manager headers (sum)'] = $mgrFan

# --- 3. Facade accessor calls -----------------------------------------------
Emit "## 3. Facade accessor calls (DirectXCommon / GraphicsCore)"
Emit ""
Emit "Counted as ``<dx-ish receiver>->Method(``. Zero means the accessor is dead code."
Emit ""
Emit "| Accessor | Calls | Files |"
Emit "|---|---:|---:|"
$accessors = @(
    'GetDevice', 'GetDXGIFactory', 'GetCommandQueue', 'GetCommandAllocator', 'GetCommandList',
    'GetCommandManager', 'GetUploadContext', 'GetSwapChain', 'GetSwapChainBackBuffer',
    'GetRTVDesc', 'GetRTVHandle', 'GetRTVHeap', 'GetSRVHeap', 'GetDSVHeap',
    'GetDepthStencilResource', 'GetDSVHandle', 'GetDepthStencilSRV',
    'GetDescriptorManager', 'GetDepthStencilManager', 'IsDXRSupported', 'GetDXRTier',
    'WaitForGpuIdle', 'GetClientWidth', 'GetClientHeight', 'RegisterResizable'
)
$facadeTotal = 0
foreach ($a in $accessors) {
    $r = Measure-FacadeAccessor -Method $a
    $facadeTotal += $r.Calls
    Emit ("| ``{0}`` | {1} | {2} |" -f $a, $r.Calls, $r.Files)
}
Emit ("| **total** | **{0}** | |" -f $facadeTotal)
Emit ""
$facadeMembers = Measure-Pattern -Pattern '(?:DirectXCommon|GraphicsCore)\s*\*\s*\w+_\s*(?:=|;)' -Set $outside -ExtFilter '.h'
Emit "Classes holding a raw facade pointer as a member (.h): **$($facadeMembers.Files)**"
Emit ""
$key['Facade accessor calls (total)'] = $facadeTotal
$key['GetDevice() calls'] = (Measure-FacadeAccessor -Method 'GetDevice').Calls
$key['Headers holding a facade pointer'] = $facadeMembers.Files

# --- 4. Descriptor API -------------------------------------------------------
Emit "## 4. Descriptor API usage (outside layer)"
Emit ""
Emit "Old out-param API should reach 0; ``Allocate*Handle`` / handle-returning API should absorb it."
Emit ""
Emit "| API | Calls | Files |"
Emit "|---|---:|---:|"
$descApis = @(
    'CreateSRV', 'CreateUAV', 'CreateCBV', 'CreateRTV', 'CreateDSV',
    'CreateOrUpdateSRV', 'CreateOrUpdateUAV',
    'AllocateSRVHandle', 'AllocateRTVHandle', 'AllocateDSVHandle',
    'FreeSRVIndex', 'FreeRTVIndex', 'FreeDSVIndex',
    'GetSRVIndexFromCpuHandle', 'GetRTVIndexFromCpuHandle', 'GetDSVIndexFromCpuHandle'
)
$oldDescApis = @('CreateSRV', 'CreateUAV', 'CreateCBV', 'CreateRTV', 'CreateDSV', 'CreateOrUpdateSRV', 'CreateOrUpdateUAV')
$newDescApis = @('AllocateSRVHandle', 'AllocateRTVHandle', 'AllocateDSVHandle')
$oldDescTotal = 0
$newDescTotal = 0
foreach ($a in $descApis) {
    $r = Measure-MemberCall -Method $a
    if ($oldDescApis -contains $a) { $oldDescTotal += $r.Calls }
    if ($newDescApis -contains $a) { $newDescTotal += $r.Calls }
    Emit ("| ``{0}`` | {1} | {2} |" -f $a, $r.Calls, $r.Files)
}
$setHeaps = Measure-MemberCall -Method 'SetDescriptorHeaps'
$heapStart = Measure-Pattern -Pattern '(?:->|\.)\s*Get(?:CPU|GPU)DescriptorHandleForHeapStart\s*\(' -Set $outside
Emit ("| ``SetDescriptorHeaps`` | {0} | {1} |" -f $setHeaps.Calls, $setHeaps.Files)
Emit ("| ``Get(CPU\|GPU)DescriptorHandleForHeapStart`` (heap poking) | {0} | {1} |" -f $heapStart.Calls, $heapStart.Files)
Emit ""
$key['Old out-param descriptor API calls'] = $oldDescTotal
$key['Handle-returning descriptor API calls'] = $newDescTotal
$key['SetDescriptorHeaps call sites'] = $setHeaps.Calls
$key['Descriptor heap poked directly'] = $heapStart.Calls

# --- 5. Barriers and resource state -----------------------------------------
Emit "## 5. Barriers and resource-state tracking (outside layer)"
Emit ""
Emit "| Metric | Calls | Files |"
Emit "|---|---:|---:|"
$barrierRows = @(
    @{ Label = 'ResourceBarrierHelper::Transition'; Pattern = 'ResourceBarrierHelper::Transition' },
    @{ Label = 'ResourceBarrierHelper::UAV';        Pattern = 'ResourceBarrierHelper::UAV' },
    @{ Label = 'ResourceBarrierBatch';              Pattern = 'ResourceBarrierBatch\s+\w+' },
    @{ Label = 'BarrierBatch (new)';                Pattern = '(?<!Resource)BarrierBatch\s+\w+' },
    @{ Label = 'ScopedResourceBarrier';             Pattern = 'ScopedResourceBarrier\s+\w+' },
    @{ Label = 'raw ->ResourceBarrier(';            Pattern = '->\s*ResourceBarrier\s*\(' }
)
$rawBarriers = 0
foreach ($b in $barrierRows) {
    $r = Measure-Pattern -Pattern $b.Pattern -Set $outside
    if ($b.Label -eq 'raw ->ResourceBarrier(') { $rawBarriers = $r.Calls }
    Emit ("| {0} | {1} | {2} |" -f $b.Label, $r.Calls, $r.Files)
}
$stateMembers = Measure-Pattern -Pattern 'D3D12_RESOURCE_STATES\s+\w*[sS]tate\w*_\s*(?:=|;|\{)' -Set $outside -ExtFilter '.h'
Emit ("| ``D3D12_RESOURCE_STATES xxxState_`` members (.h) | {0} | {1} |" -f $stateMembers.Calls, $stateMembers.Files)
Emit ""
$key['Hand-tracked resource-state members'] = $stateMembers.Calls
$key['Raw ResourceBarrier() calls'] = $rawBarriers
if ($stateMembers.Files -gt 0) {
    Emit "Headers still tracking resource state by hand:"
    Emit ""
    foreach ($p in ($stateMembers.Paths | Sort-Object)) { Emit "- ``$p``" }
    Emit ""
}

# --- 6. Raw D3D12 calls ------------------------------------------------------
Emit "## 6. Raw D3D12 calls outside the layer"
Emit ""
Emit "| Call | Calls | Files |"
Emit "|---|---:|---:|"
$rawCalls = @(
    'CreateShaderResourceView', 'CreateUnorderedAccessView', 'CreateConstantBufferView',
    'CreateRenderTargetView', 'CreateDepthStencilView', 'CreateCommittedResource',
    'CreateDescriptorHeap', 'CreateCommandAllocator', 'CreateCommandList', 'CreateFence',
    'CreateGraphicsPipelineState', 'CreateComputePipelineState', 'CreateRootSignature',
    'GetDescriptorHandleIncrementSize', 'ExecuteCommandLists', 'CreateSwapChainForHwnd', 'ResizeBuffers'
)
foreach ($c in $rawCalls) {
    $r = Measure-Pattern -Pattern ('->\s*' + [regex]::Escape($c) + '\s*\(') -Set $outside
    Emit ("| ``{0}`` | {1} | {2} |" -f $c, $r.Calls, $r.Files)
}
$factoryBuf = Measure-Pattern -Pattern 'CreateBufferResource\s*\(' -Set $outside
$factoryTex = Measure-Pattern -Pattern 'CreateTextureResource\s*\(' -Set $outside
$persistMap = Measure-Pattern -Pattern '->\s*Map\s*\(\s*0\s*,' -Set $outside
Emit ("| ``ResourceFactory::CreateBufferResource`` | {0} | {1} |" -f $factoryBuf.Calls, $factoryBuf.Files)
Emit ("| ``ResourceFactory::CreateTextureResource`` | {0} | {1} |" -f $factoryTex.Calls, $factoryTex.Files)
Emit ("| ``->Map(0, ...)`` persistent maps | {0} | {1} |" -f $persistMap.Calls, $persistMap.Files)
Emit ""

# --- 7. Frame indexing hazard ------------------------------------------------
Emit "## 7. Frame indexing (the two-sources-of-truth hazard)"
Emit ""
Emit "``GetCurrentBackBufferIndex()`` must only pick a back buffer. Using it to index"
Emit "per-frame CPU-written resources races with the GPU (it resets to 0 on ResizeBuffers)."
Emit ""
$bbIndex = Measure-Pattern -Pattern 'GetCurrentBackBufferIndex\s*\(\s*\)' -Set $outside
Emit "| Metric | Calls | Files |"
Emit "|---|---:|---:|"
Emit ("| ``GetCurrentBackBufferIndex()`` | {0} | {1} |" -f $bbIndex.Calls, $bbIndex.Files)
$recIndex = Measure-Pattern -Pattern 'GetRecordingFrameIndex\s*\(\s*\)' -Set $outside
Emit ("| ``GetRecordingFrameIndex()`` | {0} | {1} |" -f $recIndex.Calls, $recIndex.Files)
Emit ""
if ($bbIndex.Files -gt 0) {
    Emit "Files calling ``GetCurrentBackBufferIndex()``:"
    Emit ""
    foreach ($p in ($bbIndex.Paths | Sort-Object)) { Emit "- ``$p``" }
    Emit ""
}
$frameConsts = Measure-Pattern -Pattern '(?:kFrameCount|kFrameBufferCount|kMaxFramesInFlight|framesInFlight)\s*=\s*\d+' -Set $files
Emit "Independent frame-count constants declared: **$($frameConsts.Calls)** in $($frameConsts.Files) files"
Emit ""
$key['GetCurrentBackBufferIndex() calls'] = $bbIndex.Calls
$key['Independent frame-count constants'] = $frameConsts.Calls
if ($frameConsts.Files -gt 0) {
    foreach ($p in ($frameConsts.Paths | Sort-Object)) { Emit "- ``$p``" }
    Emit ""
}

# --- 8. Layering violations --------------------------------------------------
Emit "## 8. Layering violations (layer -> upper-layer includes)"
Emit ""
Emit "The foundation layer should only depend on Utility/Logger and itself."
Emit ""
$allowed = '^(?:pch\.h|Graphics/(?:Common|RHI)/|Utility/Logger/)'
$layerNames = @{}
foreach ($lf in $inLayer) { $layerNames[$lf.Name] = $true }
$violations = @()
foreach ($f in $inLayer) {
    foreach ($m in [regex]::Matches($f.Text, '#\s*include\s*"([^"]+)"')) {
        $inc = $m.Groups[1].Value
        $incNorm = $inc -replace '\\', '/'
        if ($incNorm -match $allowed) { continue }
        # a bare filename that also exists in the layer is an intra-layer include
        $leaf = Split-Path $incNorm -Leaf
        if ($layerNames.ContainsKey($leaf) -and ($incNorm -eq $leaf)) { continue }
        $violations += [pscustomobject]@{ From = $f.Rel; Include = $inc }
    }
}
Emit "Count: **$($violations.Count)**"
Emit ""
if ($violations.Count -gt 0) {
    Emit "| From | Includes |"
    Emit "|---|---|"
    foreach ($v in ($violations | Sort-Object From, Include)) {
        Emit ("| ``{0}`` | ``{1}`` |" -f $v.From, $v.Include)
    }
    Emit ""
}
$usingNs = Measure-Pattern -Pattern 'using\s+namespace\s+' -Set $inLayer -ExtFilter '.h'
Emit "``using namespace`` in layer headers (leaks to every includer): **$($usingNs.Calls)**"
Emit ""
$key['Layering violations (layer -> upper)'] = $violations.Count
$key['using namespace in layer headers'] = $usingNs.Calls

# --- 9. d3d12.h leakage ------------------------------------------------------
Emit "## 9. d3d12.h leakage into non-graphics headers"
Emit ""
$gameHeaders = $files | Where-Object {
    $_.Ext -eq '.h' -and -not ($_.Rel -like '*Graphics*') -and -not ($_.Rel -like '*Editor*')
}
$leak = Measure-Pattern -Pattern '#\s*include\s*<d3d12\.h>' -Set $gameHeaders
Emit "Non-graphics, non-editor headers including ``<d3d12.h>`` directly: **$($leak.Files)**"
Emit ""
if ($leak.Files -gt 0) {
    foreach ($p in ($leak.Paths | Sort-Object)) { Emit "- ``$p``" }
    Emit ""
}
$key['Non-graphics headers including d3d12.h'] = $leak.Files

# ------------------------------------------------------------------
# Key indicators (inserted at the top of the snapshot)
# ------------------------------------------------------------------
$summary = New-Object System.Collections.Generic.List[string]
$summary.Add('## 0. Key indicators') | Out-Null
$summary.Add('') | Out-Null
$summary.Add('Paste the previous snapshot''s column next to this one to see phase progress.') | Out-Null
$summary.Add('') | Out-Null
$summary.Add('| Indicator | Value |') | Out-Null
$summary.Add('|---|---:|') | Out-Null
foreach ($k in $key.Keys) { $summary.Add(("| {0} | {1} |" -f $k, $key[$k])) | Out-Null }
$summary.Add('') | Out-Null
$lines.InsertRange($summaryAnchor, $summary)

Write-Host ""
Write-Host "===== Key indicators ====="
foreach ($k in $key.Keys) { Write-Host ("  {0,-42} {1}" -f $k, $key[$k]) }

# ------------------------------------------------------------------
# Write snapshot
# ------------------------------------------------------------------
if (-not $NoFile) {
    if (-not $OutFile) {
        $dir = Join-Path $Root 'Docs\Engine\Graphics\Common\Metrics'
        if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        $OutFile = Join-Path $dir ("Metrics_{0}.md" -f (Get-Date -Format 'yyyy-MM-dd_HHmm'))
    }
    $parent = Split-Path $OutFile -Parent
    if ($parent -and -not (Test-Path $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    [System.IO.File]::WriteAllLines($OutFile, $lines, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ""
    Write-Host "Snapshot written: $OutFile"
}
