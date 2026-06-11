#!/usr/bin/env python3
"""
bridge.py - Le CSV do Pico 2 via serial (CDC USB) e expoe como servidor DSU
            (Cemuhook protocol) para o Dolphin via UDP 127.0.0.1:26760.

Uso:
    pip install pyserial
    python bridge.py --port COM5 --baud 115200

No Dolphin:
    Config -> Controllers -> Alternate Input Sources:
        Server List -> Add  ->  127.0.0.1 : 26760
    Em Wii Remote 1 -> Configure -> Device, escolher DSUClient/0/<...>

Formato de entrada esperado (CDC do Pico):
    ax,ay,az,gx,gy,gz\\n   (inteiros raw 16-bit do MPU6050)

Notas:
    - Implementacao minima do DSU server (apenas o necessario pro Dolphin).
    - Constantes de escala convertem raw -> unidades fisicas:
        accel raw / 16384.0  ->  g       (range +/- 2g)
        gyro  raw / 131.0    ->  deg/s   (range +/- 250 dps)
    - Protocolo: https://v1993.github.io/cemuhook-protocol/
"""

import argparse
import socket
import struct
import sys
import threading
import time
import zlib

try:
    import serial
except ImportError:
    print("ERRO: pyserial nao instalado. Rode:  pip install pyserial",
          file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Escalas do MPU6050 (defaults pos-reset: +/- 2g, +/- 250 dps)
# ---------------------------------------------------------------------------
ACCEL_LSB_PER_G    = 16384.0
GYRO_LSB_PER_DPS   = 131.0


# ---------------------------------------------------------------------------
# Estado global do motion (atualizado pelo thread serial)
# ---------------------------------------------------------------------------
# Sinais de inversao por eixo (1.0 normal, -1.0 invertido). Ajustados via CLI.
FLIP = {
    "ax": 1.0, "ay": 1.0, "az": 1.0,
    "gx": 1.0, "gy": 1.0, "gz": 1.0,
}


class MotionState:
    def __init__(self):
        self.lock = threading.Lock()
        self.accel_g    = (0.0, 0.0, 0.0)
        self.gyro_dps   = (0.0, 0.0, 0.0)
        self.last_update = time.monotonic()

    def update(self, ax, ay, az, gx, gy, gz):
        with self.lock:
            self.accel_g  = (FLIP["ax"] * ax / ACCEL_LSB_PER_G,
                             FLIP["ay"] * ay / ACCEL_LSB_PER_G,
                             FLIP["az"] * az / ACCEL_LSB_PER_G)
            self.gyro_dps = (FLIP["gx"] * gx / GYRO_LSB_PER_DPS,
                             FLIP["gy"] * gy / GYRO_LSB_PER_DPS,
                             FLIP["gz"] * gz / GYRO_LSB_PER_DPS)
            self.last_update = time.monotonic()

    def snapshot(self):
        with self.lock:
            return (self.accel_g, self.gyro_dps)


state = MotionState()


# ---------------------------------------------------------------------------
# Serial reader thread
# ---------------------------------------------------------------------------
def serial_reader(port, baud, verbose):
    print(f"[serial] abrindo {port} @ {baud}")
    try:
        ser = serial.Serial(port, baud, timeout=1.0)
    except Exception as e:
        print(f"[serial] ERRO ao abrir {port}: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"[serial] aberto. aguardando dados...")
    last_log = time.monotonic()
    samples = 0

    while True:
        try:
            line = ser.readline().decode(errors="ignore").strip()
        except Exception as e:
            print(f"[serial] ERRO leitura: {e}", file=sys.stderr)
            time.sleep(0.5)
            continue

        if not line:
            continue

        parts = line.split(",")
        if len(parts) != 6:
            continue
        try:
            ax, ay, az, gx, gy, gz = (int(p) for p in parts)
        except ValueError:
            continue

        state.update(ax, ay, az, gx, gy, gz)
        samples += 1

        now = time.monotonic()
        if verbose and (now - last_log) >= 1.0:
            a, g = state.snapshot()
            print(f"[serial] {samples} amostras/s | "
                  f"accel=({a[0]:+.2f},{a[1]:+.2f},{a[2]:+.2f})g  "
                  f"gyro=({g[0]:+6.1f},{g[1]:+6.1f},{g[2]:+6.1f})dps")
            samples = 0
            last_log = now


# ---------------------------------------------------------------------------
# DSU server (Cemuhook protocol minimo)
# ---------------------------------------------------------------------------
# Tipos de mensagem
DSU_MSG_VERSION   = 0x100000
DSU_MSG_INFO      = 0x100001
DSU_MSG_DATA      = 0x100002

SERVER_ID = 0xDEADBEEF       # qualquer u32 unico do server


def make_header(msg_type, payload_len, client_id=0):
    """Header DSU de 16 bytes + 4 bytes CRC (calculado depois)."""
    # bytes 0..3 : "DSUS" (server -> client)
    # bytes 4..5 : protocol version (1001)
    # bytes 6..7 : length = payload_len + 4 (msg_type) -- nao inclui header de 16
    # bytes 8..11: CRC32 (preenchido depois)
    # bytes 12..15: server id
    # bytes 16..19: msg_type
    header = bytearray(20)
    header[0:4]   = b"DSUS"
    header[4:6]   = struct.pack("<H", 1001)
    header[6:8]   = struct.pack("<H", payload_len + 4)
    header[8:12]  = b"\x00\x00\x00\x00"  # CRC, will be filled
    header[12:16] = struct.pack("<I", SERVER_ID)
    header[16:20] = struct.pack("<I", msg_type)
    return header


def finalize_packet(packet):
    """Calcula CRC32 sobre o pacote todo (com CRC field zerado) e injeta."""
    packet[8:12] = b"\x00\x00\x00\x00"
    crc = zlib.crc32(bytes(packet)) & 0xFFFFFFFF
    packet[8:12] = struct.pack("<I", crc)
    return bytes(packet)


def build_version_reply():
    pkt = make_header(DSU_MSG_VERSION, 2)
    pkt.extend(struct.pack("<H", 1001))
    return finalize_packet(pkt)


def build_info_reply(slot):
    pkt = make_header(DSU_MSG_INFO, 12)
    pkt.append(slot & 0xFF)              # slot
    if slot == 0:
        pkt.append(2)                    # state: 2 = connected
        pkt.append(2)                    # model: 2 = full gyro
        pkt.append(2)                    # connection: 2 = USB
        pkt.extend(b"\xDE\xAD\xBE\xEF\xCA\xFE")  # MAC fake (6 bytes)
        pkt.append(5)                    # battery: 5 = full
    else:
        pkt.append(0)                    # disconnected
        pkt.append(0)
        pkt.append(0)
        pkt.extend(b"\x00\x00\x00\x00\x00\x00")
        pkt.append(0)
    pkt.append(0)                        # terminator
    return finalize_packet(pkt)


_data_motion_timestamp_start = time.monotonic()

def build_data_reply(slot, packet_num, accel_g, gyro_dps):
    # Layout do payload DATA (80 bytes apos o message type):
    #   1 slot, 1 state, 1 model, 1 connection, 6 MAC, 1 battery, 1 connected,
    #   4 packet_num, 20 buttons/sticks, 6 touch1, 6 touch2,
    #   8 motion_timestamp, 12 accel (3x f32), 12 gyro (3x f32)
    payload_len = 80
    pkt = make_header(DSU_MSG_DATA, payload_len)

    # --- bloco "shared response" (16 bytes) ---
    pkt.append(slot & 0xFF)
    pkt.append(2)                                # state: connected
    pkt.append(2)                                # model: full gyro
    pkt.append(2)                                # connection: bluetooth (Wii Remote alike)
    pkt.extend(b"\xDE\xAD\xBE\xEF\xCA\xFE")      # MAC
    pkt.append(5)                                # battery: full

    # --- payload especifico do DATA ---
    pkt.append(1)                                # connected/active (1 = ativo)
    pkt.extend(struct.pack("<I", packet_num & 0xFFFFFFFF))  # 4 bytes

    # Buttons + sticks (20 bytes).
    #   1 buttons_1, 1 buttons_2, 1 ps, 1 touch_btn,
    #   1 lx, 1 ly, 1 rx, 1 ry,              <-- sticks PRECISAM ficar centrados em 128!
    #   1 dpad L, 1 dpad D, 1 dpad R, 1 dpad U,
    #   1 Y, 1 B, 1 A, 1 X, 1 R1, 1 L1, 1 R2, 1 L2
    pkt.extend(b"\x00\x00\x00\x00")          # buttons1/2/ps/touch_btn
    pkt.extend(b"\x80\x80\x80\x80")          # LX=128, LY=128, RX=128, RY=128 (centered)
    pkt.extend(b"\x00" * 12)                 # 12 analog buttons (dpad + face + L/R)

    # Touch 1: active, id, x (u16), y (u16) = 6 bytes
    pkt.extend(b"\x00" * 6)
    # Touch 2: idem = 6 bytes
    pkt.extend(b"\x00" * 6)

    # Motion timestamp (microseconds, u64) - relativo ao start do bridge.
    ts_us = int((time.monotonic() - _data_motion_timestamp_start) * 1e6)
    pkt.extend(struct.pack("<Q", ts_us & 0xFFFFFFFFFFFFFFFF))

    # Accel X/Y/Z em g (float32 little-endian)
    pkt.extend(struct.pack("<fff", accel_g[0], accel_g[1], accel_g[2]))

    # Gyro pitch/yaw/roll em deg/s (float32)
    pkt.extend(struct.pack("<fff", gyro_dps[0], gyro_dps[1], gyro_dps[2]))

    # Sanity check em tempo de desenvolvimento.
    assert len(pkt) == 20 + payload_len, \
        f"DATA packet len={len(pkt)} esperado={20 + payload_len}"

    return finalize_packet(pkt)


def dsu_server(udp_port, verbose):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", udp_port))
    print(f"[dsu] escutando em UDP 0.0.0.0:{udp_port}")

    clients = {}  # addr -> last_seen

    def push_data():
        """Empurra pacotes DATA pra todos os clients ativos a ~100Hz."""
        period = 1.0 / 100.0
        last = 0.0
        packet_num = 0
        logged_first = False
        while True:
            now = time.monotonic()
            sleep = period - (now - last)
            if sleep > 0:
                time.sleep(sleep)
            last = time.monotonic()

            if not clients:
                continue

            a, g = state.snapshot()
            stale = []
            for addr, ts in list(clients.items()):
                if last - ts > 5.0:
                    stale.append(addr)
                    continue
                packet_num += 1
                pkt = build_data_reply(0, packet_num, a, g)
                if verbose and not logged_first:
                    print(f"[dsu] PRIMEIRO DATA enviado: {len(pkt)} bytes "
                          f"para {addr}, packet_num={packet_num}")
                    print(f"      hex: {pkt.hex()}")
                    logged_first = True
                try:
                    sock.sendto(pkt, addr)
                except Exception as e:
                    if verbose:
                        print(f"[dsu] sendto erro: {e}")
            for addr in stale:
                clients.pop(addr, None)
                if verbose:
                    print(f"[dsu] client {addr} timed out")

    threading.Thread(target=push_data, daemon=True).start()

    while True:
        try:
            data, addr = sock.recvfrom(1024)
        except Exception as e:
            print(f"[dsu] recv erro: {e}", file=sys.stderr)
            continue

        if len(data) < 20:
            continue
        if data[0:4] != b"DSUC":
            continue

        msg_type = struct.unpack("<I", data[16:20])[0]

        if msg_type == DSU_MSG_VERSION:
            sock.sendto(build_version_reply(), addr)
            if verbose:
                print(f"[dsu] VERSION request from {addr}")
        elif msg_type == DSU_MSG_INFO:
            # Payload: u32 num_ports, depois N bytes de slot indices
            if len(data) >= 24:
                num_ports = struct.unpack("<I", data[20:24])[0]
                for i in range(num_ports):
                    if 24 + i < len(data):
                        slot = data[24 + i]
                        sock.sendto(build_info_reply(slot), addr)
            if verbose:
                print(f"[dsu] INFO request from {addr}")
        elif msg_type == DSU_MSG_DATA:
            # Cliente registrou interesse em pacotes DATA. Marca como ativo.
            clients[addr] = time.monotonic()
            if verbose and len(clients) == 1:
                print(f"[dsu] client {addr} registrado para DATA")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="Pico 2 CDC -> DSU bridge para Dolphin")
    ap.add_argument("--port", required=True, help="Porta serial (ex: COM5 ou /dev/ttyACM0)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud (padrao 115200, CDC ignora)")
    ap.add_argument("--udp-port", type=int, default=26760, help="Porta UDP DSU (padrao 26760)")
    ap.add_argument("-v", "--verbose", action="store_true", help="Log detalhado")
    # Inversao de eixo: passe a flag pra inverter o sinal do eixo correspondente.
    ap.add_argument("--flip-ax", action="store_true", help="Inverte accel X")
    ap.add_argument("--flip-ay", action="store_true", help="Inverte accel Y")
    ap.add_argument("--flip-az", action="store_true", help="Inverte accel Z")
    ap.add_argument("--flip-gx", action="store_true", help="Inverte gyro X (pitch)")
    ap.add_argument("--flip-gy", action="store_true", help="Inverte gyro Y (yaw)")
    ap.add_argument("--flip-gz", action="store_true", help="Inverte gyro Z (roll)")
    args = ap.parse_args()

    # Aplica inversoes selecionadas.
    for axis in ("ax", "ay", "az", "gx", "gy", "gz"):
        if getattr(args, f"flip_{axis}"):
            FLIP[axis] = -1.0
            print(f"[bridge] eixo {axis} INVERTIDO")

    t_ser = threading.Thread(target=serial_reader,
                             args=(args.port, args.baud, args.verbose),
                             daemon=True)
    t_ser.start()

    try:
        dsu_server(args.udp_port, args.verbose)
    except KeyboardInterrupt:
        print("\n[bridge] encerrando")


if __name__ == "__main__":
    main()
