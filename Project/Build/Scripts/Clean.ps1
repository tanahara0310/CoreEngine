# Clean.ps1
# ビルドの中間ファイル・キャッシュ・ログなど、作り直せる物を消す。
# 消す場所は下の一覧に決め打ちで書いてあり、一覧に無い物には触れない。
#
# 使い方（リポジトリ直下の Clean.bat から呼ぶ）:
#   Clean.bat                 段を選ぶメニューを出す
#   Clean.bat 2               段 2 までを消す（消す前に確認する）
#   Clean.bat 3 -DryRun       段 3 までの一覧を出すだけで消さない
#
# 段:
#   1  消しても困らない物（Debug 構成の中間ファイル・古いログ・古い設定）
#   2  1 に加えてキャッシュ（次のビルドと起動が遅くなる）
#   3  2 に加えて exe と書き出したゲーム（ビルドと書き出しがやり直しになる）

param(
    [ValidateSet(0, 1, 2, 3)]
    [int]$Level = 0,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$IgnoreCase = [System.StringComparison]::OrdinalIgnoreCase

# ---------------------------------------------------------------------------
# リポジトリの根
# ---------------------------------------------------------------------------
$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..")).TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $Root "Project\CoreEngine.vcxproj"))) {
    Write-Host "リポジトリの場所が分かりません: $Root" -ForegroundColor Red
    exit 1
}

$ProjectFolders = @()
$ProjectsDir = Join-Path $Root "Projects"
if (Test-Path -LiteralPath $ProjectsDir) {
    $ProjectFolders = @(Get-ChildItem -LiteralPath $ProjectsDir -Directory | ForEach-Object { "Projects\$($_.Name)" })
}

# ---------------------------------------------------------------------------
# 消す物（根からの相対）
# ---------------------------------------------------------------------------
$Targets = @(
    @{ Level = 1; Path = "generated\CoreEngine\obj\Debug";         Note = "Debug 構成の中間ファイル" }
    @{ Level = 1; Path = "generated\CoreEngine\outputs\Debug";     Note = "Debug 構成の exe" }
    @{ Level = 1; Path = "generated\DirectXTex\obj\Debug";         Note = "Debug 構成の中間ファイル" }
    @{ Level = 1; Path = "generated\DirectXTex\outputs\Debug";     Note = "Debug 構成のライブラリ" }
    @{ Level = 1; Path = "Project\Intermediate\Logs";              Note = "エンジン直下の古いログ" }
    @{ Level = 1; Path = "Project\Application\Saved";              Note = "プロジェクトを分ける前のエディタ設定" }

    @{ Level = 2; Path = "generated\CoreEngine\obj\Development";   Note = "中間ファイル（次のビルドが全部やり直し）" }
    @{ Level = 2; Path = "generated\CoreEngine\obj\Release";       Note = "中間ファイル（次のビルドが全部やり直し）" }
    @{ Level = 2; Path = "generated\DirectXTex\obj\Development";   Note = "中間ファイル（次のビルドが全部やり直し）" }
    @{ Level = 2; Path = "generated\DirectXTex\obj\Release";       Note = "中間ファイル（次のビルドが全部やり直し）" }
    @{ Level = 2; Path = "generated\CoreEngine\outputs\Release\Intermediate"; Note = "Release の exe の隣のキャッシュとログ" }
    @{ Level = 2; Path = "Project\.vs";                            Note = "Visual Studio のキャッシュ（次に開くと解析し直す）" }
    @{ Level = 2; Path = "Project\Intermediate\ShaderCache";       Note = "シェーダのキャッシュ（次の起動でコンパイルし直す）" }
    @{ Level = 2; Path = "Project\Intermediate\FontCache";         Note = "フォントのキャッシュ（次の起動で作り直す）" }

    @{ Level = 3; Path = "generated\CoreEngine\outputs\Development"; Note = "エンジンの exe（ビルドし直すまで起動できない）" }
    @{ Level = 3; Path = "generated\CoreEngine\outputs\Release";     Note = "エンジンの exe（ビルドし直すまで起動できない）" }
    @{ Level = 3; Path = "generated\DirectXTex\outputs\Development"; Note = "ライブラリ（次のビルドで作り直す）" }
    @{ Level = 3; Path = "generated\DirectXTex\outputs\Release";     Note = "ライブラリ（次のビルドで作り直す）" }
)
foreach ($project in $ProjectFolders) {
    $Targets += @{ Level = 1; Path = "$project\Intermediate\Logs";                      Note = "ログ" }
    $Targets += @{ Level = 1; Path = "$project\Intermediate\StartupTimings.txt";        Note = "起動時間の記録" }
    $Targets += @{ Level = 1; Path = "$project\Application\Config\EngineSettings\_backup"; Note = "設定の古いバックアップ" }
    $Targets += @{ Level = 2; Path = "$project\Intermediate\DerivedData";               Note = "テクスチャとメッシュのキャッシュ（初回起動が遅くなる）" }
    $Targets += @{ Level = 2; Path = "$project\Intermediate\TextureCache";              Note = "空のキューブマップのキャッシュ（次の起動で作り直す）" }
    $Targets += @{ Level = 3; Path = "$project\Build";                                  Note = "書き出したゲーム（書き出し直しが要る）" }
}

# ---------------------------------------------------------------------------
# 残す物（git は無視しているが作り直せない物・個人の状態）
# 消す物がこれと重なっていたら、その消す物は飛ばす
# ---------------------------------------------------------------------------
$Protected = @(
    ".git"
    "Project\Docs"
    "Project\CLAUDE.md"
    "Project\Saved"
    "Project\packages"
    "Project\Externals"
    "Project\Intermediate\ShaderManifest.txt"
    "Project\Intermediate\imgui.ini"
    "Project\CoreEngine.vcxproj.user"
)
foreach ($project in $ProjectFolders) {
    $Protected += "$project\Application\Saved"
}
$ProtectedFull = @($Protected | ForEach-Object { [System.IO.Path]::GetFullPath((Join-Path $Root $_)) })

$UseGit = [bool](Get-Command git -ErrorAction SilentlyContinue) -and (Test-Path -LiteralPath (Join-Path $Root ".git"))

# ---------------------------------------------------------------------------
# 補助
# ---------------------------------------------------------------------------
function Format-Size {
    param([long]$Bytes)
    if ($Bytes -ge 1GB) { return "{0:N1} GB" -f ($Bytes / 1GB) }
    if ($Bytes -ge 1MB) { return "{0:N0} MB" -f ($Bytes / 1MB) }
    if ($Bytes -ge 1KB) { return "{0:N0} KB" -f ($Bytes / 1KB) }
    return "$Bytes B"
}

function Test-Inside {
    param([string]$Path, [string]$Folder)
    return $Path.StartsWith($Folder + "\", $IgnoreCase)
}

# 合計の大きさを数える。リンク（ジャンクション・シンボリックリンク）の先へは入らない
function Measure-Target {
    param([string]$FullPath)
    $item = Get-Item -LiteralPath $FullPath -Force
    if (-not $item.PSIsContainer) {
        return [pscustomobject]@{ Bytes = [long]$item.Length; HasLink = $false }
    }
    $bytes = [long]0
    $hasLink = $false
    $stack = New-Object System.Collections.Stack
    $stack.Push([System.IO.DirectoryInfo]$item)
    while ($stack.Count -gt 0) {
        $dir = $stack.Pop()
        foreach ($entry in $dir.EnumerateFileSystemInfos()) {
            if ($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                $hasLink = $true
                continue
            }
            if ($entry -is [System.IO.DirectoryInfo]) {
                $stack.Push($entry)
            } else {
                $bytes += $entry.Length
            }
        }
    }
    return [pscustomobject]@{ Bytes = $bytes; HasLink = $hasLink }
}

# 消してよいかを調べる。だめなら理由を返す
function Get-SkipReason {
    param([string]$Relative, [string]$FullPath)
    if (-not (Test-Inside $FullPath $Root)) {
        return "リポジトリの外"
    }
    foreach ($protected in $ProtectedFull) {
        if ([string]::Equals($protected, $FullPath, $IgnoreCase) -or (Test-Inside $protected $FullPath) -or (Test-Inside $FullPath $protected)) {
            return "残す物に重なる"
        }
    }
    if ($UseGit) {
        $tracked = & git -C $Root ls-files -- $Relative.Replace('\', '/')
        if ($LASTEXITCODE -eq 0 -and $tracked) {
            return "git が管理しているファイルがある"
        }
    }
    return $null
}

function Get-BlockingProcesses {
    $names = @("devenv", "cl", "link")
    foreach ($process in Get-Process) {
        $path = $null
        try { $path = $process.Path } catch { }
        if ($names -contains $process.ProcessName -or ($path -and (Test-Inside $path $Root))) {
            $process
        }
    }
}

# ---------------------------------------------------------------------------
# 段を決める
# ---------------------------------------------------------------------------
$LevelTitles = @{
    1 = "段 1  消しても困らない物"
    2 = "段 2  キャッシュ（次のビルドと起動が遅くなる）"
    3 = "段 3  exe と書き出したゲーム（ビルドと書き出しがやり直しになる）"
}

if ($Level -eq 0) {
    Write-Host "CoreEngine の不要なファイルを消します。"
    Write-Host ""
    Write-Host "  1  不要な物だけ（Debug 構成の中間ファイル・古いログ・古い設定）"
    Write-Host "  2  1 + キャッシュ（次のビルドと起動が遅くなる）"
    Write-Host "  3  2 + exe と書き出したゲーム（ビルドと書き出しがやり直しになる）"
    Write-Host "  0  やめる"
    Write-Host ""
    $answer = (Read-Host "番号を入れてください").Trim()
    if ($answer -notin @("1", "2", "3")) {
        Write-Host "やめました。"
        exit 0
    }
    $Level = [int]$answer
}

# ---------------------------------------------------------------------------
# 起動中なら止める
# ---------------------------------------------------------------------------
$blocking = @(Get-BlockingProcesses)
if ($blocking.Count -gt 0) {
    Write-Host ""
    Write-Host "次のプログラムが動いています。閉じてからもう一度実行してください。" -ForegroundColor Yellow
    foreach ($process in $blocking) {
        $path = $null
        try { $path = $process.Path } catch { }
        Write-Host ("  {0}（{1}）" -f $process.ProcessName, $(if ($path) { $path } else { "場所は不明" }))
    }
    if (-not $DryRun) {
        exit 1
    }
    Write-Host "（一覧を出すだけなので続けます）"
}

# ---------------------------------------------------------------------------
# 消す物の一覧
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "大きさを数えています..."

$plan = foreach ($target in $Targets) {
    if ($target.Level -gt $Level) { continue }
    $full = [System.IO.Path]::GetFullPath((Join-Path $Root $target.Path))
    if (-not (Test-Path -LiteralPath $full)) { continue }
    $reason = Get-SkipReason $target.Path $full
    $bytes = [long]0
    if (-not $reason) {
        try {
            $measured = Measure-Target $full
            $bytes = $measured.Bytes
            if ($measured.HasLink) { $reason = "中にリンクがある" }
        } catch {
            $reason = "中を読めない"
        }
    }
    [pscustomobject]@{
        Level    = $target.Level
        Relative = $target.Path
        Full     = $full
        Note     = $target.Note
        Bytes    = $bytes
        Skip     = $reason
    }
}
$plan = @($plan)

# 別の消す物の中にある物は外側でまとめて消す
$plan = @($plan | Where-Object {
    $inner = $_
    -not ($plan | Where-Object { $_ -ne $inner -and -not $_.Skip -and (Test-Inside $inner.Full $_.Full) })
})

$toDelete = @($plan | Where-Object { -not $_.Skip })
if ($toDelete.Count -eq 0) {
    Write-Host "消す物はありません。"
    exit 0
}

foreach ($level in 1..$Level) {
    $items = @($plan | Where-Object { $_.Level -eq $level })
    if ($items.Count -eq 0) { continue }
    Write-Host ""
    Write-Host $LevelTitles[$level] -ForegroundColor Cyan
    foreach ($item in $items) {
        if ($item.Skip) {
            Write-Host ("  {0,9}  {1}  （消さない: {2}）" -f "-", $item.Relative, $item.Skip) -ForegroundColor DarkGray
        } else {
            Write-Host ("  {0,9}  {1}  {2}" -f (Format-Size $item.Bytes), $item.Relative, $item.Note)
        }
    }
}

$total = [long]0
foreach ($item in $toDelete) { $total += $item.Bytes }
Write-Host ""
Write-Host ("合計 {0}（{1} か所）" -f (Format-Size $total), $toDelete.Count)

if ($DryRun) {
    Write-Host ""
    Write-Host "一覧を出しただけです（-DryRun のため消していません）。"
    exit 0
}

# ---------------------------------------------------------------------------
# 消す
# ---------------------------------------------------------------------------
Write-Host ""
$confirm = (Read-Host "消しますか？ (Y/N)").Trim()
if ($confirm -notin @("y", "Y")) {
    Write-Host "やめました。"
    exit 0
}

$freed = [long]0
$failed = @()
foreach ($item in $toDelete) {
    Write-Host "  消しています: $($item.Relative)"
    $message = $null
    for ($attempt = 0; $attempt -lt 2 -and (Test-Path -LiteralPath $item.Full); $attempt++) {
        if ($attempt -gt 0) { Start-Sleep -Milliseconds 500 }
        $removeErrors = $null
        Remove-Item -LiteralPath $item.Full -Recurse -Force -ErrorAction SilentlyContinue -ErrorVariable removeErrors
        if ($removeErrors) { $message = $removeErrors[0].Exception.Message }
    }
    $left = [long]0
    if (Test-Path -LiteralPath $item.Full) {
        try { $left = (Measure-Target $item.Full).Bytes } catch { $left = $item.Bytes }
        $failed += [pscustomobject]@{ Relative = $item.Relative; Message = $message }
    }
    $freed += [Math]::Max([long]0, $item.Bytes - $left)
}

Write-Host ""
Write-Host ("空いた容量: {0}" -f (Format-Size $freed)) -ForegroundColor Green
if ($failed.Count -gt 0) {
    Write-Host ""
    Write-Host "消しきれなかった物があります:" -ForegroundColor Yellow
    foreach ($item in $failed) {
        Write-Host "  $($item.Relative)"
        if ($item.Message) { Write-Host "    $($item.Message)" -ForegroundColor DarkGray }
    }
    exit 1
}
exit 0
