import subprocess
import re
import sys
import serial.tools.list_ports

def get_connection_type(vid, pid):
    # Native USB CDC/JTAG is usually 303A:1001 for ESP32-S3
    if vid == 0x303A and pid == 0x1001:
        return "USB OTG (Nativo / USB-Serial-JTAG)"
    # CH340, CP2102, FTDI, etc. are external UART bridges
    elif vid == 0x1A86:
        return "UART (Paso a traves de chip CH340)"
    elif vid == 0x10C4:
        return "UART (Paso a traves de chip CP210x)"
    elif vid == 0x0403:
        return "UART (Paso a traves de chip FTDI)"
    else:
        return "UART (Paso a traves de puente generico)"

def run_command(cmd):
    try:
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=True)
        return result.stdout + "\n" + result.stderr
    except Exception as e:
        return str(e)

def scan_and_detect():
    print("==========================================================")
    print("[INFO] Escaneando puertos COM del sistema...")
    print("==========================================================")
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("[ERROR] No se encontraron puertos COM activos.")
        return

    detected_any = False
    for p in ports:
        vid = p.vid
        pid = p.pid
        
        if vid is not None and pid is not None:
            if vid in [0x303A, 0x1A86, 0x10C4, 0x0403] or "usb" in p.description.lower() or "ch340" in p.description.lower() or "cp210" in p.description.lower():
                print(f"\n[DETECTADO] Puerto: {p.device}")
                print(f"   - Descripcion: {p.description}")
                print(f"   - VID:PID: {hex(vid).upper()}:{hex(pid).upper()}")
                
                conn_type = get_connection_type(vid, pid)
                print(f"   - Tipo de Conexion: {conn_type}")
                
                # Run esptool to query flash size and chip info
                print("   - Consultando informacion del chip (esptool)...")
                esptool_out = run_command(f"{sys.executable} -m esptool --port {p.device} flash_id")
                
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
                
                # Run espefuse to query PSRAM configuration
                print("   - Consultando eFuses de memoria (espefuse)...")
                espefuse_out = run_command(f"{sys.executable} -m espefuse --port {p.device} summary")
                
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
                
                # Determine PSRAM Type, Size and Reserved Pins
                psram_info = "Sin PSRAM"
                reserved_pins = []
                
                if psram_cap in ["8M", "8MB", "16M", "16MB"]:
                    psram_info = f"{psram_cap} (Octal OPI - Alta Velocidad)"
                    reserved_pins = [33, 34, 35, 36, 37, 47, 48]
                elif psram_cap in ["2M", "2MB"]:
                    psram_info = f"{psram_cap} (Quad QSPI)"
                    reserved_pins = [33, 34, 35, 36, 37]
                else:
                    psram_info = "No detectada o deshabilitada en eFuse"
                    reserved_pins = []
                
                print("\n[RESULTADO DEL DIAGNOSTICO]")
                print(f"   ======================================================")
                print(f"   * Chip:             {chip_type}")
                print(f"   * MAC Address:      {mac}")
                print(f"   * Memoria Flash:    {flash_size} ({flash_lines})")
                print(f"   * Memoria PSRAM:    {psram_info}")
                if reserved_pins:
                    print(f"   * ADVERTENCIA - PINES RESERVADOS: {', '.join(map(str, reserved_pins))}")
                    print(f"     (Prohibido usar estos pines para otros perifericos o causara crash)")
                else:
                    print(f"   * PINES RESERVADOS: Ninguno en headers externos (todos los GPIOs libres)")
                print(f"   ======================================================")
                detected_any = True
                
    if not detected_any:
        print("[INFO] No se detectaron tarjetas ESP32 compatibles conectadas.")

if __name__ == "__main__":
    scan_and_detect()
