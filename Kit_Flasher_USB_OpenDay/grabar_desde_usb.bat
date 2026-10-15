@echo off
setlocal enabledelayedexpansion
title FLASHEADOR AUTONOMO USB - Physys Lab ESP32-S3 (Open Day)
cd /d "%~dp0"
cls
echo ======================================================================
echo   FLASHEADOR AUTONOMO USB-C - PHYSYS LAB ESP32-S3
echo   Version: V_1_16_07_26  (Tag: v1.17-open-day)
echo ======================================================================
echo   [OK] Totalmente portatil. NO requiere Python ni PlatformIO.
echo ======================================================================
echo.

if not exist "tools\esptool.exe" (
    echo [ERROR] No se encuentra tools\esptool.exe en este paquete.
    echo Asegurate de descomprimir todo el contenido de la USB.
    pause
    exit /b 1
)

echo [1] Escaneando puertos COM conectados...
set DEFAULT_PORT=
for /f "usebackq delims=" %%p in (`powershell -NoProfile -Command "[System.IO.Ports.SerialPort]::GetPortNames() ^| Select-Object -Unique"`) do (
    echo   * Detectado: %%p
    if not defined DEFAULT_PORT set DEFAULT_PORT=%%p
)

if defined DEFAULT_PORT (
    set /p PORT="? Ingresa el puerto COM a usar [Sugerido: !DEFAULT_PORT!]: "
    if "!PORT!"=="" set PORT=!DEFAULT_PORT!
) else (
    echo   [AVISO] No se detecto ningun puerto COM conectado.
    set /p PORT="? Ingresa manualmente el puerto COM (ejemplo: COM6): "
)

if "%PORT%"=="" (
    echo [ERROR] Debes indicar un puerto COM valido.
    pause
    exit /b 1
)
echo --> Puerto seleccionado: %PORT%
echo.

echo ======================================================================
echo [2] Selecciona el perfil de hardware:
echo   1^) Base / Clon  (ESP32-S3 estandar, PSRAM QSPI o sin PSRAM)
echo   2^) Tarjeta CAM  (ESP32-S3 con modulo de camara, PSRAM Octal OPI)
echo ======================================================================
set /p TIPO="? Elige una opcion (1 o 2) [Default: 1]: "
if "%TIPO%"=="2" (
    set CARPETA=cam
    set PERFIL=Tarjeta CAM (Octal OPI)
) else (
    set CARPETA=base
    set PERFIL=Base / Clon (QSPI)
)
echo --> Perfil seleccionado: %PERFIL%
echo.

echo ======================================================================
echo [3] Selecciona la accion a realizar:
echo   1^) Grabacion Completa (Firmware + Web LittleFS - Conserva NVS)
echo   2^) Modo Rescate (Borrado Completo de Flash previo + Grabacion)
echo   3^) Solo Asignar / Cambiar Mesa por Serie (Sin regrabar firmware)
echo ======================================================================
set /p MODO="? Elige una opcion (1, 2 o 3) [Default: 1]: "
if "%MODO%"=="" set MODO=1

if "%MODO%"=="3" goto ASIGNAR_MESA_ONLY

if "%MODO%"=="2" (
    echo.
    echo ======================================================================
    echo   [MODO RESCATE] Borrando toda la memoria flash del ESP32-S3...
    echo ======================================================================
    tools\esptool.exe --chip esp32s3 --port %PORT% erase_flash
    if errorlevel 1 goto ERROR_FLASH
)

echo.
echo ======================================================================
echo   INICIANDO GRABACION EN %PORT% (%PERFIL%)...
echo   Por favor, no desconectes la tarjeta durante el proceso.
echo ======================================================================
echo.

tools\esptool.exe --chip esp32s3 --port %PORT% --baud 460800 --before default_reset --after hard_reset write_flash -z --flash_mode qio --flash_freq 80m --flash_size 16MB 0x0 bin\%CARPETA%\bootloader.bin 0x8000 bin\%CARPETA%\partitions.bin 0xd000 bin\%CARPETA%\boot_app0.bin 0x10000 bin\%CARPETA%\firmware.bin 0x610000 bin\%CARPETA%\littlefs.bin

if errorlevel 1 goto ERROR_FLASH

echo.
echo ======================================================================
echo  ?? ?GRABACION COMPLETADA CON EXITO! ??
echo ======================================================================
echo.
echo Esperando a que el ESP32-S3 reinicie en modo normal (4 segundos)...
powershell -NoProfile -Command "Start-Sleep -Seconds 4"

:ASIGNAR_MESA_ONLY
echo.
echo ======================================================================
echo [4] ASIGNACION DE IDENTIDAD DE MESA (WPA2 + Canal no interferente)
echo ======================================================================
set /p NMESA="? Ingresa el numero de mesa para esta tarjeta (1-99, 0 = abierta, Enter = omitir): "

if "%NMESA%"=="" (
    echo [INFO] Identidad de mesa conservada sin cambios.
    goto FIN
)

echo [INFO] Enviando comando SET_MESA:%NMESA% a %PORT%...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$p = New-Object System.IO.Ports.SerialPort '%PORT%', 115200, 'None', 8, 'One'; " ^
  "$p.DtrEnable = $false; $p.RtsEnable = $false; $p.ReadTimeout = 2500; " ^
  "try { $p.Open(); Start-Sleep -Milliseconds 300; $p.WriteLine('SET_MESA:%NMESA%'); Start-Sleep -Milliseconds 600; $line = $p.ReadLine(); Write-Host ('   Respuesta ESP32: ' + $line); $p.Close(); } " ^
  "catch { Write-Host ('   [AVISO] No se pudo comunicar por serie: ' + $_.Exception.Message); }"

echo.
echo [OK] Configuracion de mesa enviada.
echo Recuerda consultar la carpeta 'QR_Mesas' para imprimir el codigo QR correspondiente.
goto FIN

:ERROR_FLASH
echo.
echo ======================================================================
echo   [FALLO] Ocurrio un error durante la grabacion.
echo   Sugerencias de solucion:
echo   1. Manten presionado el boton BOOT, conecta el USB y reintenta.
echo   2. Si tu tarjeta tiene dos conectores USB, prueba el otro puerto.
echo   3. Verifica que no haya otro programa usando el puerto %PORT%.
echo ======================================================================
pause
exit /b 1

:FIN
echo.
echo ======================================================================
echo   PROCESO TERMINADO. Ya puedes desconectar tu tarjeta.
echo ======================================================================
pause
exit /b 0
