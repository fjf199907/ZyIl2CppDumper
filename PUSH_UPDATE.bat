@echo off
setlocal EnableExtensions DisableDelayedExpansion
title Push ZyIl2CppDumper update
set "REPO=%~dp0"
set "PROXY=http://127.0.0.1:12000"
set "CHECK_ONLY=0"
if /I "%~1"=="--check" set "CHECK_ONLY=1"

rem Work only in the independent repository beside this file. Never use a parent repo.
if not exist "%REPO%.git" (
    echo ERROR: No .git in this folder. Put this BAT inside ZyIl2CppDumper-git.
    goto failed
)
where git >nul 2>&1
if errorlevel 1 (
    echo ERROR: Git is not available in PATH.
    goto failed
)
git -C "%REPO%." rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 goto failed
git -C "%REPO%." symbolic-ref --quiet --short HEAD
if errorlevel 1 (
    echo ERROR: Detached HEAD. Switch to your working branch first.
    goto failed
)
set "REMOTE="
for /f "delims=" %%R in ('git -C "%REPO%." remote get-url origin') do set "REMOTE=%%R"
if /I not "%REMOTE%"=="https://github.com/fjf199907/ZyIl2CppDumper.git" (
    echo ERROR: origin is not the expected GitHub repository.
    goto failed
)
echo Repository: %REPO%
echo Proxy: %PROXY%
git -C "%REPO%." status --short
if errorlevel 1 goto failed
if "%CHECK_ONLY%"=="1" (
    echo CHECK OK: Local checks only. No staging, commit, or push performed.
    exit /b 0
)

echo.
echo [1/3] Stage local changes, including additions and deletions...
git -C "%REPO%." add --all -- .
if errorlevel 1 goto failed
git -C "%REPO%." diff --cached --quiet --exit-code
if errorlevel 2 goto failed
if errorlevel 1 (
    echo [2/3] Commit local changes...
    git -C "%REPO%." commit -m "Update local project %DATE% %TIME%"
    if errorlevel 1 goto failed
) else (
    echo [2/3] No new changes. Existing unpushed commits will still be pushed.
)

echo [3/3] Push through proxy...
git -C "%REPO%." -c "http.proxy=%PROXY%" -c http.lowSpeedLimit=1 -c http.lowSpeedTime=30 push --progress origin HEAD
if errorlevel 1 (
    echo ERROR: Push failed. Your local commit is preserved.
    echo Check proxy/login/network. If remote changes caused rejection, integrate them before retrying.
    goto failed
)
echo.
echo SUCCESS: Update pushed.
git -C "%REPO%." log -1 --oneline
echo Start a NEW Build workflow on GitHub Actions to compile this commit.
pause
exit /b 0

:failed
echo.
echo FAILED: Read the error above. No force-push or reset was performed.
if "%CHECK_ONLY%"=="0" pause
exit /b 1
