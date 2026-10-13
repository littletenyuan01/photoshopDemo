@echo off
setlocal enabledelayedexpansion
rem ============================================================================
rem build-shaders.bat -- rebuild engine/backend/shaders/*.comp into *.comp.qsb
rem
rem The .qsb files are checked-in binaries. Their generation command was NOT
rem recorded anywhere, and they drifted in three ways that produced NO build
rem error -- only "GPU silently falls back to CPU" or "wrong pixels nobody
rem notices":
rem   * custom.comp.qsb had only DXBC 50, missing the HLSL 50 that the D3D11
rem     runtime looks for;
rem   * noise.comp.qsb was older than noise.comp (source changed, never baked);
rem   * boxblur.comp itself normalized by n instead of n^2 (see gpu-plan T5.4).
rem The `gpukernels` test case now guards this: it fails if any of the four
rem packs is unparseable or missing a required target.
rem
rem Usage:
rem   psDemo\tools\build-shaders.bat
rem   set QSB=D:\Qt\6.11.0\mingw_64\bin\qsb.exe
rem   psDemo\tools\build-shaders.bat
rem
rem Target set is fixed at  --glsl "310 es,440" --hlsl 50 --msl 12
rem   HLSL 50 -> QRhi D3D11 (the main path on this machine)
rem   GLSL    -> QRhi OpenGLES2 fallback and `qsb -d` self-check
rem   MSL 12  -> macOS, should anyone ever run it
rem All four packs MUST be baked with the same flags. median/noise used HLSL
rem while custom used DXBC; each looked fine until the other backend was tried.
rem ============================================================================

set "HERE=%~dp0"
set "SHADERDIR=%HERE%..\engine\backend\shaders"

if not defined QSB (
    for %%Q in (
        "D:\Qt\6.11.0\mingw_64\bin\qsb.exe"
        "D:\Qt\6.11.0\msvc2022_64\bin\qsb.exe"
        "C:\Qt\6.11.0\mingw_64\bin\qsb.exe"
        "C:\Qt\6.11.0\msvc2022_64\bin\qsb.exe"
    ) do (
        if not defined QSB if exist %%Q set "QSB=%%~Q"
    )
)
if not defined QSB (
    for %%P in ("qsb.exe") do if not defined QSB set "QSB=%%~$PATH:P"
)
if not defined QSB (
    echo [build-shaders] qsb.exe not found. Set QSB=^<path^>\qsb.exe and retry.
    exit /b 1
)

echo [build-shaders] qsb = %QSB%

set FAILED=0
for %%S in (median custom noise boxblur) do (
    echo [build-shaders] baking %%S.comp
    "%QSB%" --glsl "310 es,440" --hlsl 50 --msl 12 "%SHADERDIR%\%%S.comp" -o "%SHADERDIR%\%%S.comp.qsb"
    if errorlevel 1 (
        echo [build-shaders] FAILED: %%S.comp
        set FAILED=1
    )
)

if not "%FAILED%"=="0" (
    echo [build-shaders] one or more shaders failed.
    exit /b 1
)
echo [build-shaders] done. Rebuild the project (qmake need not be re-run).
endlocal
