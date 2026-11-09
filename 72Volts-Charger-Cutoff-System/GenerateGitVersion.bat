@echo off
setlocal EnableExtensions
set "SKETCH=%~dp0"
REM Optional Visual Micro build.path; sketch sources may be copied before prebuild.
set "BUILD=%~1"
set "BUILDDIR="
if defined BUILD for %%I in ("%BUILD%\.") do set "BUILDDIR=%%~fI"
for %%I in ("%SKETCH%.") do set "SKETCHDIR=%%~fI"
set "OUT=%SKETCH%GitVersion.generated.h"
set "TMP=%SKETCH%GitVersion.generated.tmp"
set "GITTMP=%TEMP%\GpmGitVersion_%RANDOM%_%RANDOM%.txt"
set "FAIL_DEFINE=INVALID_GIT_BUILD"
git -C "%SKETCH%." rev-parse --show-toplevel > "%GITTMP%" 2>nul
if errorlevel 1 goto fail
set /p REPO=<"%GITTMP%"
if not defined REPO goto fail
git -C "%REPO%" branch --show-current > "%GITTMP%" 2>nul
if errorlevel 1 goto fail
set /p BRANCH=<"%GITTMP%"
if not defined BRANCH goto fail
echo Git branch: %BRANCH%
if /I "%BRANCH:~0,8%"=="release/" set "FAIL_DEFINE=INVALID_RELEASE_BUILD"
:generate_version
REM Always use the real firmware commit SHA on every branch.
git -C "%REPO%" rev-parse --short=7 HEAD > "%GITTMP%"
if errorlevel 1 goto fail
set /p HASH=<"%GITTMP%"
if not defined HASH goto fail
set "VERSION=%HASH%"
REM An asterisk marks non-production branches, including release/*.
if /I not "%BRANCH%"=="main" if /I not "%BRANCH%"=="master" set "VERSION=%HASH%*"
> "%TMP%" echo #pragma once
REM Synchronize local Git library revisions before completing this firmware build.
REM The shared .gpm/avr-libraries.json travels with commits through PRs and releases.
REM The library helper appends its macro; the firmware macro is written afterward
REM so a manifest updated by the helper is marked with X as locally modified.
powershell.exe -NoProfile -STA -NonInteractive -ExecutionPolicy Bypass -File "%SKETCH%SyncAvrLibraries.ps1"
if errorlevel 1 goto fail
REM The helper has checked dirty worktrees and revision locks first.
REM Fetch/alignment are mandatory for published releases, not local-only ones.
if /I not "%BRANCH:~0,8%"=="release/" goto release_remote_checked
REM ls-remote exits 2 only when the release ref is absent; transport/auth errors
REM must still fail closed rather than being mistaken for an unpublished release.
git -C "%REPO%" ls-remote --exit-code --heads origin "refs/heads/%BRANCH%" > "%GITTMP%" 2>nul
if errorlevel 3 goto fail
if errorlevel 2 goto release_remote_checked
if errorlevel 1 goto fail
git -C "%REPO%" fetch origin "%BRANCH%" --quiet
if errorlevel 1 goto fail
git -C "%REPO%" rev-parse HEAD > "%GITTMP%"
if errorlevel 1 goto fail
set /p LOCAL=<"%GITTMP%"
git -C "%REPO%" rev-parse FETCH_HEAD > "%GITTMP%"
if errorlevel 1 goto fail
set /p REMOTE=<"%GITTMP%"
if /I not "%LOCAL%"=="%REMOTE%" goto fail
:release_remote_checked
REM Dirty repositories are validated by the library helper on all branches.
if /I "%BRANCH:~0,8%"=="release/" goto publish_version
if /I "%BRANCH%"=="main" goto publish_version
if /I "%BRANCH%"=="master" goto publish_version
REM Ignore only Visual Studio project files for the build Dirty marker; keep them Git-tracked.
git -C "%REPO%" status --porcelain --untracked-files=all -- . ":(exclude,glob)**/*.vcxproj" > "%GITTMP%"
if errorlevel 1 goto fail
for %%A in ("%GITTMP%") do if not "%%~zA"=="0" set "VERSION=%VERSION%X"
:publish_version
>> "%TMP%" echo #define GIT_VERSION "%VERSION%"
echo Git version: %VERSION%
move /y "%TMP%" "%OUT%" >nul
if errorlevel 1 goto fail
REM Visual Micro copies the sketch into build.path before some build hooks.
REM Keep the generated header used by the compiler in sync on the FIRST build.
if not defined BUILDDIR goto success
if not exist "%BUILDDIR%\" goto fail
if /I "%BUILDDIR%"=="%SKETCHDIR%" goto success
copy /y "%OUT%" "%BUILDDIR%\GitVersion.generated.h" >nul
if errorlevel 1 goto fail
REM Arduino build layouts may place sketch sources under build.path\sketch.
if exist "%BUILDDIR%\sketch\" (
    copy /y "%OUT%" "%BUILDDIR%\sketch\GitVersion.generated.h" >nul
    if errorlevel 1 goto fail
)
echo Git version synced to build directory: %BUILDDIR%
:success
del "%GITTMP%" >nul 2>&1
exit /b 0
:fail
REM A failed prebuild must not poison Visual Micro's next prototype scan.
REM The nonzero exit status is the build blocker; do not persist #error in an include.
if exist "%OUT%" del /f /q "%OUT%" >nul 2>&1
REM Never leave a stale generated header in the compiler workspace on failure.
if defined BUILDDIR if /I not "%BUILDDIR%"=="%SKETCHDIR%" (
    if exist "%BUILDDIR%\GitVersion.generated.h" del /f /q "%BUILDDIR%\GitVersion.generated.h" >nul 2>&1
    if exist "%BUILDDIR%\sketch\GitVersion.generated.h" del /f /q "%BUILDDIR%\sketch\GitVersion.generated.h" >nul 2>&1
)
del "%TMP%" >nul 2>&1
del "%GITTMP%" >nul 2>&1
echo ERROR: %FAIL_DEFINE% - Cannot generate GitVersion.generated.h from Git state.
exit /b 1
