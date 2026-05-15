@echo off
setlocal

echo ============================================
echo   SimpleTorrent - Build Script
echo ============================================

:: Check for vcpkg
if not defined VCPKG_ROOT (
    echo [ERRO] VCPKG_ROOT nao definido.
    echo Defina: set VCPKG_ROOT=C:\caminho\para\vcpkg
    exit /b 1
)

echo [1/3] Configurando CMake...
cmake -B build -S . ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static

if errorlevel 1 (
    echo [ERRO] CMake falhou.
    exit /b 1
)

echo [2/3] Compilando...
cmake --build build --config Release --parallel

if errorlevel 1 (
    echo [ERRO] Build falhou.
    exit /b 1
)

echo [3/3] Build completo!
echo Executavel: build\Release\SimpleTorrent.exe

echo.
echo Para criar o instalador, abra installer\setup.iss no Inno Setup.
