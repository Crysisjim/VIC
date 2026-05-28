@echo off
setlocal enabledelayedexpansion

:: === CONFIGURATION ===
set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build"
set "CONFIG=Release"
set "TARGET=VIC"
set "VCPKG_TOOLCHAIN=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

:: Retire le backslash final de PROJECT_DIR pour eviter les problemes de guillemets
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

:: === CHARGEMENT MSVC ===
echo ============================================
echo   V.I.C - Video Image Comparator v2.1 - Build Script
echo ============================================
echo.
echo [1/4] Chargement des outils Visual Studio 2022...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    echo ERREUR : vcvars64.bat non trouve.
    echo Lance ce .bat depuis "Developer Command Prompt" ou verifie le chemin VS.
    pause
    exit /b 1
)
echo       OK

:: === NETTOYAGE ===
echo [2/4] Nettoyage du build precedent...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%" 2>nul
mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
echo       OK

:: === CMAKE ===
echo [3/4] Configuration CMake...
cmake "%PROJECT_DIR%" -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DVCPKG_TARGET_TRIPLET=x64-windows -G "Visual Studio 17 2022" -A x64

if %errorlevel% neq 0 (
    echo.
    echo ERREUR : Configuration CMake echouee !
    pause
    exit /b 1
)
echo       OK

:: === COMPILATION ===
echo [4/4] Compilation %CONFIG%...
cmake --build . --config %CONFIG% --parallel

if %errorlevel% neq 0 (
    echo.
    echo =============================================
    echo   ECHEC COMPILATION - Voir les erreurs ci-dessus
    echo =============================================
    pause
    exit /b 1
)

:: === COPIE DES ASSETS ===
set "OUT_DIR=%BUILD_DIR%\%CONFIG%"
if not exist "%OUT_DIR%\assets" mkdir "%OUT_DIR%\assets"
if exist "%PROJECT_DIR%\assets\backroom.jpg" copy /y "%PROJECT_DIR%\assets\backroom.jpg" "%OUT_DIR%\assets\" >nul
if exist "%PROJECT_DIR%\assets\icon.ico" copy /y "%PROJECT_DIR%\assets\icon.ico" "%OUT_DIR%\assets\" >nul

:: === SUCCES ===
echo.
echo =============================================
echo   BUILD REUSSI !
echo   Exe: %OUT_DIR%\%TARGET%.exe
echo =============================================
echo.

:: === LANCEMENT ===
set "EXE=%OUT_DIR%\%TARGET%.exe"
if exist "%EXE%" (
    echo Lancement...
    start "" "%EXE%"
) else (
    echo ATTENTION: EXE non trouve a : %EXE%
)

endlocal
pause
