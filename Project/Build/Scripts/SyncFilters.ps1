# SyncFilters.ps1
# Synchronizes .vcxproj.filters with the actual folder hierarchy on disk.
# Only writes the file if changes are detected; does nothing when up-to-date.
#
# Standalone usage:
#   .\SyncFilters.ps1
#   .\SyncFilters.ps1 -ProjectFile "C:\...\CoreEngine.vcxproj"
#
# Pre-build event (added automatically to CoreEngine.vcxproj):
#   powershell -ExecutionPolicy Bypass -NoProfile -File
#     "$(ProjectDir)Scripts\SyncFilters.ps1"
#     -ProjectFile "$(MSBuildProjectFullPath)"

param(
    [string]$ProjectFile = ""
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Resolve paths
# ---------------------------------------------------------------------------
if ([string]::IsNullOrEmpty($ProjectFile)) {
    $searchDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
    $found = Get-ChildItem -Path $searchDir -Filter "*.vcxproj" -Depth 1 |
             Where-Object { $_.Extension -eq ".vcxproj" } |
             Select-Object -First 1
    if (-not $found) {
        Write-Error "[SyncFilters] No *.vcxproj found. Specify -ProjectFile."
        exit 1
    }
    $ProjectFile = $found.FullName
}

$ProjectFile = [System.IO.Path]::GetFullPath($ProjectFile)
$FiltersFile = "$ProjectFile.filters"

if (-not (Test-Path $ProjectFile)) {
    Write-Error "[SyncFilters] Not found: $ProjectFile"
    exit 1
}
if (-not (Test-Path $FiltersFile)) {
    Write-Error "[SyncFilters] Not found: $FiltersFile"
    exit 1
}

# ---------------------------------------------------------------------------
# Item types to track
# ---------------------------------------------------------------------------
$ItemTypes = @("ClCompile", "ClInclude", "None")

# ---------------------------------------------------------------------------
# Helper: derive expected filter from a relative file path
#         = the directory part of the path (backslash-separated)
#         Returns $null for root-level files (e.g. main.cpp)
# ---------------------------------------------------------------------------
function Get-ExpectedFilter {
    param([string]$path)
    $dir = [System.IO.Path]::GetDirectoryName($path)
    if ([string]::IsNullOrWhiteSpace($dir)) { return $null }
    return $dir
}

# ---------------------------------------------------------------------------
# Parse vcxproj: collect ordered list of (Type, Include)
# Only includes files that actually exist on disk.
# Stale entries (not on disk) are collected for removal from .vcxproj.
# ---------------------------------------------------------------------------
[xml]$projXml = [System.IO.File]::ReadAllText($ProjectFile, [System.Text.Encoding]::UTF8)

$projectDir = Split-Path -Parent $ProjectFile
$projItems  = New-Object "System.Collections.Generic.List[hashtable]"
$staleNodes = [System.Collections.Generic.List[object]]::new()

foreach ($ig in $projXml.Project.ItemGroup) {
    foreach ($t in $ItemTypes) {
        $nodes = $ig.ChildNodes | Where-Object { $_.LocalName -eq $t -and $_.Include }
        foreach ($n in $nodes) {
            $fullPath = Join-Path $projectDir $n.Include
            if (-not (Test-Path $fullPath)) {
                Write-Host "[SyncFilters] Skip (not on disk): $($n.Include)"
                $staleNodes.Add($n)
                continue
            }
            $projItems.Add(@{ Type = $t; Include = $n.Include })
        }
    }
}

# ---------------------------------------------------------------------------
# Remove stale file references from .vcxproj
# ---------------------------------------------------------------------------
if ($staleNodes.Count -gt 0) {
    foreach ($node in $staleNodes) {
        $node.ParentNode.RemoveChild($node) | Out-Null
    }

    # Remove ItemGroups that are now empty (no element children remain)
    $emptyGroups = @($projXml.Project.ChildNodes |
        Where-Object { $_.LocalName -eq "ItemGroup" -and $_.ChildNodes.Count -eq 0 })
    foreach ($ig in $emptyGroups) {
        $projXml.Project.RemoveChild($ig) | Out-Null
    }

    # Write back the cleaned vcxproj
    $xmlSettings                    = New-Object System.Xml.XmlWriterSettings
    $xmlSettings.Indent             = $true
    $xmlSettings.IndentChars        = "  "
    $xmlSettings.NewLineChars       = "`r`n"
    $xmlSettings.Encoding           = New-Object System.Text.UTF8Encoding($false)
    $xmlSettings.OmitXmlDeclaration = $false
    $writer = [System.Xml.XmlWriter]::Create($ProjectFile, $xmlSettings)
    try   { $projXml.Save($writer) }
    finally { $writer.Close() }

    Write-Host "[SyncFilters] vcxproj cleaned ($($staleNodes.Count) stale entries removed): $(Split-Path -Leaf $ProjectFile)"
}

# ---------------------------------------------------------------------------
# Parse existing vcxproj.filters
# ---------------------------------------------------------------------------
[xml]$filtersXml = [System.IO.File]::ReadAllText($FiltersFile, [System.Text.Encoding]::UTF8)

# Include -> current filter string (or $null when no Filter element)
$existingFilters = @{}
foreach ($ig in $filtersXml.Project.ItemGroup) {
    foreach ($t in $ItemTypes) {
        $nodes = $ig.ChildNodes | Where-Object { $_.LocalName -eq $t -and $_.Include }
        foreach ($n in $nodes) {
            $fNode = $n.ChildNodes | Where-Object { $_.LocalName -eq "Filter" } | Select-Object -First 1
            $existingFilters[$n.Include] = if ($fNode) { $fNode.InnerText } else { $null }
        }
    }
}

# Preserve existing filter-hierarchy GUIDs: filterPath -> GUID string
$existingGuids = @{}
foreach ($ig in $filtersXml.Project.ItemGroup) {
    $nodes = $ig.ChildNodes | Where-Object { $_.LocalName -eq "Filter" -and $_.Include }
    foreach ($f in $nodes) {
        $guidNode = $f.ChildNodes | Where-Object { $_.LocalName -eq "UniqueIdentifier" } | Select-Object -First 1
        if ($guidNode) { $existingGuids[$f.Include] = $guidNode.InnerText }
    }
}

# ---------------------------------------------------------------------------
# Collect all needed filter-hierarchy paths (including ancestors)
# Must be built before change detection so we can compare against existing GUIDs.
# ---------------------------------------------------------------------------
$neededPaths = New-Object "System.Collections.Generic.SortedSet[string]" (
    [System.StringComparer]::OrdinalIgnoreCase)

foreach ($item in $projItems) {
    $f = Get-ExpectedFilter $item.Include
    if (-not $f) { continue }
    $parts = $f.Split('\')
    for ($i = 0; $i -lt $parts.Count; $i++) {
        $ancestor = ($parts[0..$i]) -join '\'
        $neededPaths.Add($ancestor) | Out-Null
    }
}

# ---------------------------------------------------------------------------
# Detect changes
# ---------------------------------------------------------------------------
$hasChanges = $false

# 1. Every item in vcxproj must have the correct filter
foreach ($item in $projItems) {
    $expected = Get-ExpectedFilter $item.Include
    if ($existingFilters.ContainsKey($item.Include)) {
        if ($existingFilters[$item.Include] -ne $expected) { $hasChanges = $true; break }
    } else {
        $hasChanges = $true; break
    }
}

# 2. Stale file entries: in filters but no longer in vcxproj
if (-not $hasChanges) {
    $projSet = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($item in $projItems) { $projSet.Add($item.Include) | Out-Null }
    foreach ($inc in $existingFilters.Keys) {
        if (-not $projSet.Contains($inc)) { $hasChanges = $true; break }
    }
}

# 3. Stale filter-folder entries: <Filter> in filters file but no longer needed
if (-not $hasChanges) {
    foreach ($fp in $existingGuids.Keys) {
        if (-not $neededPaths.Contains($fp)) { $hasChanges = $true; break }
    }
}

# 4. Missing filter-folder entries: needed but not yet present in filters file
if (-not $hasChanges) {
    foreach ($fp in $neededPaths) {
        if (-not $existingGuids.ContainsKey($fp)) { $hasChanges = $true; break }
    }
}

if (-not $hasChanges) {
    Write-Host "[SyncFilters] Up to date. No changes needed."
    exit 0
}

Write-Host "[SyncFilters] Changes detected. Updating filters file..."

# ---------------------------------------------------------------------------
# Build new XML content
# ---------------------------------------------------------------------------
$sb = New-Object System.Text.StringBuilder

$sb.Append('<?xml version="1.0" encoding="utf-8"?>') | Out-Null
$sb.Append("`r`n") | Out-Null
$sb.Append('<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">') | Out-Null
$sb.Append("`r`n") | Out-Null

# -- File item groups (one ItemGroup per type) --------------------------------
foreach ($t in $ItemTypes) {
    $typeItems = $projItems | Where-Object { $_.Type -eq $t }
    if (-not $typeItems) { continue }

    $sb.Append("  <ItemGroup>") | Out-Null
    $sb.Append("`r`n") | Out-Null

    foreach ($item in $typeItems) {
        $inc    = $item.Include
        $filter = Get-ExpectedFilter $inc
        if ($filter) {
            $sb.Append("    <$t Include=`"$inc`">")     | Out-Null; $sb.Append("`r`n") | Out-Null
            $sb.Append("      <Filter>$filter</Filter>") | Out-Null; $sb.Append("`r`n") | Out-Null
            $sb.Append("    </$t>")                      | Out-Null; $sb.Append("`r`n") | Out-Null
        } else {
            $sb.Append("    <$t Include=`"$inc`" />")   | Out-Null; $sb.Append("`r`n") | Out-Null
        }
    }

    $sb.Append("  </ItemGroup>") | Out-Null
    $sb.Append("`r`n") | Out-Null
}

# -- Filter hierarchy ItemGroup -----------------------------------------------
$sb.Append("  <ItemGroup>") | Out-Null
$sb.Append("`r`n") | Out-Null

foreach ($fp in $neededPaths) {
    $guid = if ($existingGuids.ContainsKey($fp)) {
        $existingGuids[$fp]
    } else {
        "{$([System.Guid]::NewGuid().ToString().ToUpper())}"
    }
    $sb.Append("    <Filter Include=`"$fp`">")                       | Out-Null; $sb.Append("`r`n") | Out-Null
    $sb.Append("      <UniqueIdentifier>$guid</UniqueIdentifier>")   | Out-Null; $sb.Append("`r`n") | Out-Null
    $sb.Append("    </Filter>")                                       | Out-Null; $sb.Append("`r`n") | Out-Null
}

$sb.Append("  </ItemGroup>") | Out-Null
$sb.Append("`r`n") | Out-Null

$sb.Append("</Project>") | Out-Null
$sb.Append("`r`n") | Out-Null

$newContent = $sb.ToString()

# ---------------------------------------------------------------------------
# Write only if content actually changed (avoid unnecessary file touches)
# ---------------------------------------------------------------------------
$oldContent = [System.IO.File]::ReadAllText($FiltersFile, [System.Text.Encoding]::UTF8)

# Normalize line endings before comparing
$normalizedOld = $oldContent -replace "`r`n", "`n" -replace "`r", "`n"
$normalizedNew = $newContent -replace "`r`n", "`n"

if ($normalizedOld -eq $normalizedNew) {
    Write-Host "[SyncFilters] Content identical after normalization. No write needed."
    exit 0
}

[System.IO.File]::WriteAllText(
    $FiltersFile,
    $newContent,
    (New-Object System.Text.UTF8Encoding($false))   # UTF-8 without BOM
)

Write-Host "[SyncFilters] Filters file updated: $FiltersFile"
