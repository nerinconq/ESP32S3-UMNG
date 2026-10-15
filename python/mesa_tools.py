"""
mesa_tools.py - Herramientas de identidad de mesa para Physys Lab (Open Day).

Funciones:
  * Calcular la clave WiFi y el canal de cada mesa (MISMO formato que src/main.cpp).
  * Hablar con la tarjeta por puerto serie: PHYSYS_INFO y SET_MESA:<n>.
  * Generar la tarjeta QR (PNG) para pegar en la seccion 4 del poster.
  * Llevar un registro CSV de que tarjeta (MAC/SSID) quedo en que mesa.

Uso directo:
  python python/mesa_tools.py asignar --port COM6 --mesa 7 [--tipo base]
  python python/mesa_tools.py info    --port COM6
  python python/mesa_tools.py qr      --mesa 7 --ssid Physys-Lab-A3F2
"""
import argparse
import csv
import os
import re
import sys
import time
from datetime import datetime

# --- Debe coincidir con src/main.cpp (mesaPassword / mesaChannel) ---
PASSWORD_FMT = "umngm-{:02d}"
CHANNELS = (1, 6, 11)
AP_IP = "192.168.4.1"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QR_DIR = os.path.join(ROOT, "QR_Mesas")
REGISTRO = os.path.join(QR_DIR, "registro_mesas.csv")


def mesa_password(n: int) -> str:
    return PASSWORD_FMT.format(n) if n > 0 else ""


def mesa_channel(n: int) -> int:
    return CHANNELS[(n - 1) % 3] if n > 0 else 1


def ssid_from_base_mac(mac: str) -> str:
    """El SSID usa la MAC del AP, que en ESP32-S3 es la MAC base (la que muestra esptool) + 1."""
    value = int(mac.replace(":", "").replace("-", ""), 16)
    ap_mac = (value + 1) & 0xFFFFFFFFFFFF
    return "Physys-Lab-{:04X}".format(ap_mac & 0xFFFF)


# ─────────────────────────── Puerto serie ───────────────────────────
def _open(port):
    import serial
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.3
    # DTR/RTS en falso: abrir el puerto NO debe reiniciar la tarjeta ni ponerla en modo BOOT
    s.dtr = False
    s.rts = False
    s.open()
    return s


def _exchange(port, command, expect_prefix, wait_s=12.0):
    """Envia un comando repetidamente hasta recibir una linea que empiece por expect_prefix."""
    deadline = time.time() + wait_s
    last_err = None
    while time.time() < deadline:
        try:
            with _open(port) as s:
                s.reset_input_buffer()
                t_send = 0
                t_end = min(deadline, time.time() + 4)
                while time.time() < t_end:
                    if time.time() - t_send > 1.0:
                        s.write((command + "\n").encode())
                        t_send = time.time()
                    line = s.readline().decode(errors="ignore").strip()
                    if line.startswith(expect_prefix):
                        return line
        except Exception as e:  # el puerto puede desaparecer mientras la tarjeta reinicia
            last_err = e
            time.sleep(1.0)
    raise TimeoutError(f"Sin respuesta '{expect_prefix}' en {port}. {last_err or ''}".strip())


def query_info(port, wait_s=12.0) -> dict:
    line = _exchange(port, "PHYSYS_INFO", "PHYSYS_INFO", wait_s)
    info = dict(re.findall(r"(\w+)=(\S*)", line))
    info["mesa"] = int(info.get("mesa", 0))
    info["canal"] = int(info.get("canal", 1))
    return info


def set_mesa(port, n: int) -> dict:
    _exchange(port, f"SET_MESA:{n}", "PHYSYS_MESA_OK")
    time.sleep(3.0)  # la tarjeta se reinicia
    info = query_info(port, wait_s=20.0)
    if info["mesa"] != n:
        raise RuntimeError(f"La tarjeta reporta mesa {info['mesa']} en vez de {n}")
    return info


# ─────────────────────────── Tarjeta QR ───────────────────────────
def _wifi_escape(text: str) -> str:
    return re.sub(r'([\\;,:"])', r"\\\1", text)


def _font(size, bold=False):
    from PIL import ImageFont
    for name in (("arialbd.ttf" if bold else "arial.ttf"), "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def make_qr_card(mesa: int, ssid: str, password: str, canal: int, out_path=None) -> str:
    import qrcode
    from PIL import Image, ImageDraw

    os.makedirs(QR_DIR, exist_ok=True)
    if out_path is None:
        out_path = os.path.join(QR_DIR, f"QR_Mesa_{mesa:02d}_{ssid}.png")

    payload = f"WIFI:T:WPA;S:{_wifi_escape(ssid)};P:{_wifi_escape(password)};;" if password \
        else f"WIFI:T:nopass;S:{_wifi_escape(ssid)};;"
    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=20, border=2)
    qr.add_data(payload)
    qr.make(fit=True)
    qr_img = qr.make_image(fill_color="black", back_color="white").convert("RGB")
    qr_img = qr_img.resize((900, 900), Image.NEAREST)

    W, H = 1000, 1400
    card = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(card)
    navy = (20, 52, 110)
    d.rectangle([0, 0, W, 170], fill=navy)
    title = f"MESA {mesa:02d}" if mesa > 0 else "SIN MESA"
    f_title = _font(110, bold=True)
    tw = d.textlength(title, font=f_title)
    d.text(((W - tw) / 2, 25), title, fill="white", font=f_title)

    card.paste(qr_img, (50, 190))

    f_lbl, f_val, f_small = _font(40), _font(46, bold=True), _font(32)
    y = 1110
    for label, value in (("Red WiFi:", ssid), ("Clave:", password or "(abierta)"), ("App:", f"http://{AP_IP}")):
        d.text((70, y), label, fill=(90, 90, 90), font=f_lbl)
        d.text((290, y - 4), value, fill="black", font=f_val)
        y += 70
    note = f"Escanea con la camara del celular  ·  canal {canal}"
    nw = d.textlength(note, font=f_small)
    d.text(((W - nw) / 2, H - 55), note, fill=(110, 110, 110), font=f_small)

    card.save(out_path, dpi=(300, 300))
    return out_path


def registrar(mesa, ssid, password, canal, tipo="", mac="", port=""):
    os.makedirs(QR_DIR, exist_ok=True)
    nuevo = not os.path.exists(REGISTRO)
    with open(REGISTRO, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if nuevo:
            w.writerow(["fecha", "mesa", "ssid", "clave", "canal", "tipo_tarjeta", "mac_base", "puerto"])
        w.writerow([datetime.now().strftime("%Y-%m-%d %H:%M"), mesa, ssid, password, canal, tipo, mac, port])


def asignar_y_generar(port, mesa, tipo="", mac=""):
    """Flujo completo: asigna la mesa en la tarjeta, lee la red REAL y genera QR + registro."""
    info = set_mesa(port, mesa)
    path = make_qr_card(info["mesa"], info["ssid"], info.get("pass", ""), info["canal"])
    registrar(info["mesa"], info["ssid"], info.get("pass", ""), info["canal"], tipo, mac, port)
    return info, path


def main():
    ap = argparse.ArgumentParser(description="Identidad de mesa Physys Lab")
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("asignar"); a.add_argument("--port", required=True); a.add_argument("--mesa", type=int, required=True); a.add_argument("--tipo", default="")
    i = sub.add_parser("info"); i.add_argument("--port", required=True)
    q = sub.add_parser("qr"); q.add_argument("--mesa", type=int, required=True); q.add_argument("--ssid", required=True)
    args = ap.parse_args()

    if args.cmd == "asignar":
        info, path = asignar_y_generar(args.port, args.mesa, args.tipo)
        print(f"[OK] Mesa {info['mesa']} | {info['ssid']} | clave {info.get('pass')} | canal {info['canal']}")
        print(f"[OK] QR: {path}")
    elif args.cmd == "info":
        print(query_info(args.port))
    elif args.cmd == "qr":
        print(make_qr_card(args.mesa, args.ssid, mesa_password(args.mesa), mesa_channel(args.mesa)))


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"[ERROR] {e}")
        sys.exit(1)
