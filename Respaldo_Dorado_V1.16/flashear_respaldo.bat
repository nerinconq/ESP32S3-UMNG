@echo off
title RESPALDO DORADO - Physys Lab V_1_16_07_26
cd /d "%~dp0"
echo ======================================================================
echo   RESPALDO DORADO - PHYSYS LAB  (version estable V_1_16_07_26)
echo   Tag Git: v1.16-estable-presentacion
echo ======================================================================
echo.
echo Requisito: Python instalado (con "Add Python to PATH").
echo.
python -m esptool version >nul 2>&1
if %errorlevel% neq 0 (
    echo [INFO] Instalando esptool...
    python -m pip install esptool
    if errorlevel 1 (
        echo [ERROR] No se pudo instalar esptool. Ejecuta: pip install esptool
        pause
        exit /b
    )
)

echo Tipo de tarjeta:
echo   1^) Base o Clon   (PSRAM QSPI)
echo   2^) CAM           (PSRAM OPI 8MB)
set /p TIPO="Elige 1 o 2: "
if "%TIPO%"=="2" (set CARPETA=cam) else (set CARPETA=base)

echo.
echo Modo de grabacion:
echo   1^) Normal   - conserva calibraciones y ajustes guardados (NVS)
echo   2^) Rescate  - BORRA TODA la memoria primero (tarjeta con problemas)
set /p MODO="Elige 1 o 2 [1]: "

set /p PORT="Puerto COM de la tarjeta (ej: COM6): "
if "%PORT%"=="" (
    echo [ERROR] Debes indicar un puerto.
    pause
    exit /b
)

if "%MODO%"=="2" (
    echo.
    echo [RESCATE] Borrando memoria flash completa...
    python -m esptool --chip esp32s3 --port %PORT% erase_flash
    if errorlevel 1 goto FALLO
)

echo.
echo Grabando carpeta "%CARPETA%" en %PORT% ...
python -m esptool --chip esp32s3 --port %PORT% --baud 460800 --before default_reset --after hard_reset write_flash -z --flash_mode qio --flash_freq 80m --flash_size 16MB 0x0 %CARPETA%\bootloader.bin 0x8000 %CARPETA%\partitions.bin 0xd000 %CARPETA%\boot_app0.bin 0x10000 %CARPETA%\firmware.bin 0x610000 %CARPETA%\littlefs.bin
if errorlevel 1 goto FALLO

echo.
echo ======================================================================
echo   GRABACION COMPLETADA. Desconecta y reconecta la tarjeta.
echo ======================================================================
pause
exit /b

:FALLO
echo.
echo ======================================================================
echo   [FALLO] Mantener presionado BOOT, conectar el USB y reintentar.
echo   Si persiste, prueba el otro puerto USB de la tarjeta (USB / UART).
echo ======================================================================
pause
