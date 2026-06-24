@echo off
setlocal

echo [1/4] Setting Git to case-sensitive mode...
git config core.ignorecase false

echo [2/4] Clearing Git index (this does NOT delete your local files)...
git rm -r --cached .

echo [3/4] Re-adding files to sync casing and respect .gitignore...
git add .

echo [4/4] Restoring Git to default case-insensitive mode (standard for Windows)...
git config core.ignorecase true

echo.
echo Normalization complete!
echo Please check "git status" to see the changes.
pause
