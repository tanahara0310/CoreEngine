# タブをスペースに変換するスクリプト
# Engine と Application フォルダ内の .cpp, .h, .c ファイルを対象にします

param(
    [int]$TabSize = 4
)

# 対象とするファイル拡張子
$extensions = @("*.cpp", "*.h", "*.c", "*.hpp", "*.cc", "*.cxx")

# 対象フォルダ
$folders = @("Engine", "Application")

$totalFiles = 0
$modifiedFiles = 0

Write-Host "=== タブからスペースへの変換を開始します ===" -ForegroundColor Green
Write-Host "タブサイズ: $TabSize スペース" -ForegroundColor Cyan
Write-Host ""

foreach ($folder in $folders) {
    if (-not (Test-Path $folder)) {
        Write-Host "警告: フォルダ '$folder' が見つかりません。スキップします。" -ForegroundColor Yellow
        continue
    }

    Write-Host "フォルダ '$folder' を処理中..." -ForegroundColor Cyan

    foreach ($extension in $extensions) {
        $files = Get-ChildItem -Path $folder -Filter $extension -Recurse -File

        foreach ($file in $files) {
            $totalFiles++
            
            try {
                # ファイルの内容を読み込む（UTF-8 BOM付きで読み込み）
                $content = Get-Content -Path $file.FullName -Raw -Encoding UTF8
                
                if ($null -eq $content) {
                    continue
                }

                # タブが含まれているかチェック
                if ($content -match "`t") {
                    # タブをスペースに変換
                    $spaces = " " * $TabSize
                    $newContent = $content -replace "`t", $spaces
                    
                    # ファイルに書き込む（UTF-8 BOM付き）
                    $utf8BOM = New-Object System.Text.UTF8Encoding $true
                    [System.IO.File]::WriteAllText($file.FullName, $newContent, $utf8BOM)
                    
                    $modifiedFiles++
                    Write-Host "  ? $($file.FullName)" -ForegroundColor Gray
                }
            }
            catch {
                Write-Host "  ? エラー: $($file.FullName) - $($_.Exception.Message)" -ForegroundColor Red
            }
        }
    }
}

Write-Host ""
Write-Host "=== 変換完了 ===" -ForegroundColor Green
Write-Host "処理したファイル数: $totalFiles" -ForegroundColor Cyan
Write-Host "変更したファイル数: $modifiedFiles" -ForegroundColor Cyan
Write-Host ""

if ($modifiedFiles -gt 0) {
    Write-Host "注意: 変更されたファイルを確認してください。" -ForegroundColor Yellow
}
