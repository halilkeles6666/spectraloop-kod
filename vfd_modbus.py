#!/usr/bin/env python3
"""
Spectraloop - Delta MS300 VFD kontrolu (Modbus RTU / RS485), Pi tarafi.
"""
import json
import threading

import serial

VFD_SERIAL_PORT = "/dev/ttyAMA3"
VFD_BAUD = 9600
VFD_SLAVE_ID = 1

VFD_PARAMS = {
    "00-11": {"addr": 0x000B, "name": "Frekans Komut Kaynagi", "unit": "", "scale": 1},
    "00-22": {"addr": 0x0016, "name": "Frekans EEPROM Kayit", "unit": "", "scale": 1},
    "00-23": {"addr": 0x0017, "name": "Son Frekansla Ac", "unit": "", "scale": 1},
    "01-00": {"addr": 0x0100, "name": "Maks Cikis Frekansi", "unit": "Hz", "scale": 0.01},
    "01-01": {"addr": 0x0101, "name": "Baz Frekans", "unit": "Hz", "scale": 0.01},
    "01-02": {"addr": 0x0102, "name": "Maks Cikis Gerilimi", "unit": "V", "scale": 0.1},
    "01-03": {"addr": 0x0103, "name": "Orta Nokta Frekansi 1", "unit": "Hz", "scale": 0.01},
    "01-04": {"addr": 0x0104, "name": "Orta Nokta Gerilimi 1", "unit": "V", "scale": 0.1},
    "01-05": {"addr": 0x0105, "name": "Min Cikis Frekansi", "unit": "Hz", "scale": 0.01},
    "01-06": {"addr": 0x0106, "name": "Min Cikis Gerilimi", "unit": "V", "scale": 0.1},
    "01-08": {"addr": 0x0108, "name": "Min Frekans Gerilimi", "unit": "V", "scale": 0.1},
    "01-10": {"addr": 0x010A, "name": "Ust Sinir Frekansi", "unit": "Hz", "scale": 0.01},
    "01-12": {"addr": 0x010C, "name": "Hizlanma Suresi", "unit": "s", "scale": 0.01},
    "01-13": {"addr": 0x010D, "name": "Yavaslama Suresi", "unit": "s", "scale": 0.01},
    "06-03": {"addr": 0x0603, "name": "Hizlanmada Asiri Akim Onleme", "unit": "%", "scale": 1},
    "06-04": {"addr": 0x0604, "name": "Sabit Hizda Asiri Akim Onleme", "unit": "%", "scale": 1},
}

_conn = None
_connected = False
_lock = threading.Lock()


def modbus_crc16(data):
    crc = 0xFFFF
    for b in bytearray(data):
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def connect():
    global _conn, _connected
    try:
        conn = serial.Serial(VFD_SERIAL_PORT, VFD_BAUD, timeout=0.5,
                              parity=serial.PARITY_NONE, stopbits=1, bytesize=8)
        print(f"[VFD] Modbus RTU baglandi: {VFD_SERIAL_PORT} @ {VFD_BAUD} baud")
        _conn = conn
        _connected = True
        return conn
    except Exception as e:
        print(f"[VFD] Seri port acilamadi ({e})")
        _conn = None
        _connected = False
        return None


def read_register(address, slave_id=VFD_SLAVE_ID):
    global _conn, _connected
    if _conn is None:
        connect()
        if _conn is None:
            return None
    frame = bytes([slave_id, 0x03]) + address.to_bytes(2, "big") + (1).to_bytes(2, "big")
    frame += modbus_crc16(frame)
    with _lock:
        try:
            _conn.reset_input_buffer()
            _conn.write(frame)
            resp = _conn.read(7)
            if len(resp) < 7:
                return None
            if resp[1] & 0x80:
                return None
            return (resp[3] << 8) | resp[4]
        except Exception as e:
            print(f"[VFD] Okuma hatasi: {e}")
            _connected = False
            try:
                _conn.close()
            except Exception:
                pass
            _conn = None
            return None


def write_register(address, value, slave_id=VFD_SLAVE_ID):
    global _conn, _connected
    if _conn is None:
        connect()
        if _conn is None:
            return False
    frame = bytes([slave_id, 0x06]) + address.to_bytes(2, "big") + int(value).to_bytes(2, "big")
    frame += modbus_crc16(frame)
    with _lock:
        try:
            _conn.reset_input_buffer()
            _conn.write(frame)
            resp = _conn.read(8)
            if len(resp) < 8:
                return False
            if resp[1] & 0x80:
                return False
            return True
        except Exception as e:
            print(f"[VFD] Yazma hatasi: {e}")
            _connected = False
            try:
                _conn.close()
            except Exception:
                pass
            _conn = None
            return False


def handle_command(text):
    try:
        return _handle_command_inner(text)
    except Exception as e:
        print(f"[VFD] Komut hatasi ({text}): {e}")
        return None


def _handle_command_inner(text):
    if text == "VFD_READ_ALL":
        results = {}
        for code, meta in VFD_PARAMS.items():
            results[code] = read_register(meta["addr"])
        return json.dumps({"type": "vfd_data", "data": results, "connected": _connected})

    if text.startswith("VFD_READ:"):
        code = text[len("VFD_READ:"):].strip()
        meta = VFD_PARAMS.get(code)
        if not meta:
            return None
        raw = read_register(meta["addr"])
        return json.dumps({"type": "vfd_data", "data": {code: raw}, "connected": _connected})

    if text.startswith("VFD_WRITE:"):
        rest = text[len("VFD_WRITE:"):].strip()
        parts = rest.split(":")
        if len(parts) != 2:
            return None
        code, raw_value_str = parts
        meta = VFD_PARAMS.get(code)
        if not meta:
            return None
        try:
            raw_value = int(raw_value_str)
        except ValueError:
            return None
        ok = write_register(meta["addr"], raw_value)
        print(f"[VFD] YAZ {code} (0x{meta['addr']:04X}) = {raw_value} -> {'OK' if ok else 'BASARISIZ'}")
        return json.dumps({"type": "vfd_write_result", "code": code, "ok": ok})

    if text.startswith("MOTOR_"):
        return _handle_motor(text)

    return None


# Delta MS300 Modbus kontrol adresleri
MOTOR_CTRL_ADDR = 0x2000
MOTOR_FREQ_ADDR = 0x2001

_motor_direction = 'forward'
_motor_freq = 0


def _handle_motor(text):
    global _motor_direction, _motor_freq

    if text == 'MOTOR_DIR_FORWARD':
        _motor_direction = 'forward'
        ok = write_register(MOTOR_CTRL_ADDR, 0x0010)
        print(f'[VFD] Yon: ILERI -> {"OK" if ok else "BASARISIZ"}')
        return json.dumps({'type': 'motor', 'data': {'direction': 'forward', 'ok': ok}})

    if text == 'MOTOR_DIR_REVERSE':
        _motor_direction = 'reverse'
        ok = write_register(MOTOR_CTRL_ADDR, 0x0020)
        print(f'[VFD] Yon: GERI -> {"OK" if ok else "BASARISIZ"}')
        return json.dumps({'type': 'motor', 'data': {'direction': 'reverse', 'ok': ok}})

    if text.startswith('MOTOR_FREQ:'):
        try:
            freq = float(text[11:].strip())
            _motor_freq = freq
            raw_freq = int(freq * 100)
            ok = write_register(MOTOR_FREQ_ADDR, raw_freq)
            print(f'[VFD] Frekans: {freq} Hz (ham: {raw_freq}) -> {"OK" if ok else "BASARISIZ"}')
            return json.dumps({'type': 'motor', 'data': {'freq': freq, 'freq_ok': ok}})
        except ValueError:
            return None

    if text == 'MOTOR_GO':
        cmd = 0x0012 if _motor_direction == 'forward' else 0x0022
        ok = write_register(MOTOR_CTRL_ADDR, cmd)
        print(f'[VFD] MOTOR CALISTIR ({_motor_direction}) -> {"OK" if ok else "BASARISIZ"}')
        return json.dumps({'type': 'motor', 'data': {'run': True, 'direction': _motor_direction, 'ok': ok}})

    if text == 'MOTOR_STOP':
        ok = write_register(MOTOR_CTRL_ADDR, 0x0001)
        print(f'[VFD] MOTOR DURDUR -> {"OK" if ok else "BASARISIZ"}')
        return json.dumps({'type': 'motor', 'data': {'run': False, 'ok': ok}})

    return None
