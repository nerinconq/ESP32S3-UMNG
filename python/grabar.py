import os
import sys
import re
import subprocess
import time
import json
import serial.tools.list_ports

def get_connection_type(vid, pid):
    if vid == 0x303A and pid == 0x1001:
        return "USB OTG (Nativo / USB-Serial-JTAG)"
    elif vid == 0x1A86:
        return "UART (Paso a traves de chip CH340)"
    elif vid == 0x10C4:
        return "UART (Paso a traves de chip CP210x)"
    elif vid == 0x0403:
        return "UART (Paso a traves de chip FTDI)"
    else:
        return "UART (Puente usb-serial generico)"

def run_command(cmd):
    try:
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=True)
        return result.stdout + "\n" + result.stderr
    except Exception as e:
        return str(e)

def analyze_device(port):
    print(f"\n[INFO] Consultando informacion de hardware en {port}...")
    
    # 1. Consultar esptool
    esptool_out = run_command(f"{sys.executable} -m esptool --port {port} flash_id")
    
    mac = "Desconocido"
    flash_size = "Desconocido"
    chip_type = "ESP32-S3"
    
    mac_match = re.search(r"MAC:\s*([0-9a-fA-F:]+)", esptool_out)
    if mac_match:
        mac = mac_match.group(1)
    
    flash_match = re.search(r"Detected flash size:\s*(\d+MB|\d+KB)", esptool_out)
    if flash_match:
        flash_size = flash_match.group(1)
        
    chip_match = re.search(r"Detecting chip type\.\.\.\s*(ESP\d+-\S+|ESP\d+)", esptool_out)
    if chip_match:
        chip_type = chip_match.group(1)
        
    # 2. Consultar espefuse
    espefuse_out = run_command(f"{sys.executable} -m espefuse --port {port} summary")
    
    psram_cap = "None"
    flash_lines = "4 data lines"
    
    psram_match = re.search(r"PSRAM_CAP\s*\(BLOCK1\)\s*.*?=\s*(\S+)", espefuse_out)
    if not psram_match:
        psram_match = re.search(r"PSRAM capacity\s*=\s*(\S+)", espefuse_out)
    if psram_match:
        psram_cap = psram_match.group(1)
        
    flash_type_match = re.search(r"FLASH_TYPE\s*\(BLOCK0\)\s*.*?=\s*(.*?)\s*(?:R/W|RO)", espefuse_out)
    if flash_type_match:
        flash_lines = flash_type_match.group(1).strip()
        
    # Clasificación de PSRAM
    is_opi = False
    psram_info = "Sin PSRAM"
    reserved_pins = []
    
    if psram_cap in ["8M", "8MB", "16M", "16MB"]:
        psram_info = f"{psram_cap} (Octal OPI - Alta Velocidad)"
        reserved_pins = [33, 34, 35, 36, 37, 47, 48]
        is_opi = True
    elif psram_cap in ["2M", "2MB"]:
        psram_info = f"{psram_cap} (Quad QSPI)"
        reserved_pins = [33, 34, 35, 36, 37]
    else:
        psram_info = "No detectada o deshabilitada en eFuse"
        
    # Sugerir entorno de PlatformIO basado en PSRAM
    # esp32s3cam utiliza OPI (opi), esp32s3base utiliza QSPI o sin PSRAM
    sug_env = "esp32s3cam" if is_opi else "esp32s3base"
    sug_name = "Tarjeta CAM (esp32s3cam)" if is_opi else "Base / Clon (esp32s3base)"
    
    print("\n======================================================")
    print(f" ⚙️  DIAGNÓSTICO FÍSICO DEL ESP32-S3:")
    print(f"   * Chip:             {chip_type}")
    print(f"   * Dirección MAC:    {mac}")
    print(f"   * Memoria Flash:    {flash_size} ({flash_lines})")
    print(f"   * Memoria PSRAM:    {psram_info}")
    if reserved_pins:
        print(f"   * PINES RESERVADOS: {', '.join(map(str, reserved_pins))}")
        print(f"     (¡ADVERTENCIA! No conectar sensores a estos pines)")
    print(f"   ======================================================")
    print(f"   * PRESELECCIÓN RECOMENDADA: {sug_name}")
    print("======================================================")
    
    return sug_env

def sync_versions_and_cache_buster():
    print("\n[INFO] Sincronizando versiones y aplicando cache buster...")
    try:
        config_path = "data/config.json"
        if not os.path.exists(config_path):
            print("  [AVISO] No se encontro data/config.json")
            return
            
        with open(config_path, "r", encoding="utf-8") as f:
            cfg = json.load(f)
            
        version = cfg.get("version", "V_1_16_07_26")
        print(f"  Version detectada: {version}")
        
        # 1. Actualizar sw.js con CACHE_NAME conteniendo un timestamp
        sw_path = "data/www/sw.js"
        if os.path.exists(sw_path):
            with open(sw_path, "r", encoding="utf-8") as f:
                sw_content = f.read()
            timestamp = int(time.time())
            new_cache_name = f"const CACHE_NAME = 'physys-{version}-{timestamp}';"
            sw_content = re.sub(r"const CACHE_NAME = 'physys-.*?';", new_cache_name, sw_content)
            with open(sw_path, "w", encoding="utf-8", newline="\n") as f:
                f.write(sw_content)
            print(f"  [OK] sw.js actualizado con cache buster: {version}-{timestamp}")
            
        # 2. Sincronizar version en app.js
        app_js_path = "data/www/app.js"
        if os.path.exists(app_js_path):
            with open(app_js_path, "r", encoding="utf-8") as f:
                app_content = f.read()
            app_content = re.sub(r"version:\s*'V_[0-9a-zA-Z_.]+'", f"version: '{version}'", app_content)
            app_content = re.sub(r"version\s*\|\|\s*'V_[0-9a-zA-Z_.]+'", f"version || '{version}'", app_content)
            with open(app_js_path, "w", encoding="utf-8", newline="\n") as f:
                f.write(app_content)
            print("  [OK] app.js sincronizado con la version.")
            
        # 3. Sincronizar version en src/main.cpp
        main_cpp_path = "src/main.cpp"
        if os.path.exists(main_cpp_path):
            with open(main_cpp_path, "r", encoding="utf-8") as f:
                cpp_content = f.read()
            cpp_content = re.sub(r"\"version\":\"V_[0-9a-zA-Z_.]+\"", f"\"version\":\"{version}\"", cpp_content)
            with open(main_cpp_path, "w", encoding="utf-8", newline="\n") as f:
                f.write(cpp_content)
            print("  [OK] src/main.cpp sincronizado con la version.")
            
    except Exception as e:
        print(f"  [ERROR] Error al sincronizar versiones: {e}")

def main():
    print("==========================================================")
    print("      🛠️  ASISTENTE DE GRABACIÓN OPTIMIZADO - PHYSYS LAB")
    print("==========================================================")
    
    # 1. Escanear puertos COM
    ports = list(serial.tools.list_ports.comports())
    detected_ports = []
    
    print("\n[1] Escaneando puertos COM conectados...")
    for p in ports:
        vid = p.vid
        pid = p.pid
        conn_type = get_connection_type(vid, pid) if vid is not None else "Puerto COM estándar"
        print(f"  * Detectado: {p.device} | {p.description} ({conn_type})")
        detected_ports.append((p.device, conn_type))
        
    if not detected_ports:
        print("[AVISO] No se detectó ningún puerto COM activo automáticamente.")
        port = input("➔ Ingresa manualmente el puerto COM a usar (ejemplo: COM3): ").strip()
        conn_type_sel = "Entrada manual"
    else:
        default_port, conn_type_sel = detected_ports[0]
        port = input(f"➔ Selecciona el puerto COM a usar (Default: {default_port}): ").strip()
        if not port:
            port = default_port
            # buscar tipo de conexión correcto para el seleccionado
            for dp, ct in detected_ports:
                if dp == port:
                    conn_type_sel = ct
                    break
            
    print(f"--> Puerto seleccionado: {port} ({conn_type_sel})")
    
    # 2. Ejecutar diagnóstico de hardware
    recommended_env = "esp32s3base"
    try:
        recommended_env = analyze_device(port)
    except Exception as e:
        print(f"[AVISO] No se pudo realizar el diagnóstico detallado: {e}")
        
    # 3. Selección del perfil / entorno
    print("\n[2] Confirma o cambia el perfil de tarjeta a grabar:")
    default_env_num = "2" if recommended_env == "esp32s3cam" else "1"
    
    print(f"  1) Base / Clon (Entorno: esp32s3base - PSRAM QSPI)")
    print(f"  2) Tarjeta CAM (Entorno: esp32s3cam - PSRAM OPI)")
    
    choice = input(f"➔ Elige una opción (1 o 2) [Sugerido: {default_env_num}]: ").strip()
    if not choice:
        choice = default_env_num
        
    if choice == '2':
        env = "esp32s3cam"
        profile_name = "Tarjeta CAM"
    else:
        env = "esp32s3base"
        profile_name = "Base / Clon"
        
    print(f"--> Perfil seleccionado para compilar: {profile_name} (Entorno PIO: {env})")
    
    # 3.5 Sincronizar versiones y aplicar cache buster
    sync_versions_and_cache_buster()
    
    # 4. Preguntar por compresión de la web
    compress = input("\n[3] ¿Deseas comprimir los archivos web antes de subir? (S/n): ").strip().lower()
    if compress != 'n':
        print("  -> Comprimiendo archivos web (gzip)...")
        try:
            res = subprocess.run([sys.executable, "scratch/gzip_assets.py"], check=True, capture_output=True, text=True)
            print(res.stdout)
            print("[OK] Compresión completada.")
        except Exception as e:
            print(f"[ERROR] Error al comprimir: {e}")
            
    # 5. Confirmación final
    print("\n==========================================================")
    print(f"   RESUMEN DE GRABACIÓN:")
    print(f"   * Puerto:        {port} ({conn_type_sel})")
    print(f"   * Perfil:        {profile_name} ({env})")
    print("==========================================================")
    confirm = input("➔ ¿Confirmas la grabación? (S/n): ").strip().lower()
    if confirm == 'n':
        print("[ABORTADO] Grabación cancelada por el usuario.")
        return

    # 6. Subir Filesystem (LittleFS)
    print("\n[4] Construyendo interfaz web (LittleFS)...")
    cmd_build_fs = f"pio run -e {env} -t buildfs"
    print(f"Ejecutando: {cmd_build_fs}")
    build_fs_res = subprocess.run(cmd_build_fs, shell=True)
    if build_fs_res.returncode != 0:
        print("\n[ERROR] Falló la construcción del Filesystem LittleFS.")
        return

    print("\n[5] Subiendo interfaz web (LittleFS) al ESP32...")
    cmd_fs = f"pio run -e {env} -t uploadfs --upload-port {port}"
    print(f"Ejecutando: {cmd_fs}")
    fs_res = subprocess.run(cmd_fs, shell=True)
    
    if fs_res.returncode != 0:
        print("\n[ERROR] Falló la subida del Filesystem.")
        print("Sugerencia: Revisa la conexión, puerto USB correcto (USB vs UART) o pon la tarjeta en modo BOOT manualmente.")
        return

    # 7. Subir Firmware
    print("\n[6] Subiendo firmware al ESP32...")
    cmd_fw = f"pio run -e {env} -t upload --upload-port {port}"
    print(f"Ejecutando: {cmd_fw}")
    fw_res = subprocess.run(cmd_fw, shell=True)
    
    if fw_res.returncode == 0:
        print("\n==========================================================")
        print(" 🎉 ¡PROCESO DE GRABACIÓN FINALIZADO CON ÉXITO! 🎉")
        print("==========================================================")
    else:
        print("\n[ERROR] Falló la subida del firmware.")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[ABORTADO] Proceso cancelado por teclado.")
