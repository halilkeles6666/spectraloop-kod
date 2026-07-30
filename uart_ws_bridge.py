#!/usr/bin/env python3
import asyncio
import base64
import hashlib
import json
import os
import socket
import struct
import termios
import time

SERIAL_PORT = "/dev/ttyTHS1"
SERIAL_BAUD = 115200
WS_HOST = "0.0.0.0"
WS_PORT = 5006
# Sabit yol kullanilir: bu CH340 adaptoru guc/USB dalgalanmalarinda sik sik
# yeniden numaralaniyor (ttyUSB0 <-> ttyUSB1). by-id sembolik baglantisi
# hangi numaraya duserse dussun ayni cihazi gosterir.
ARDUINO_PORT = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
ARDUINO_BAUD = 115200
HEARTBEAT_TIMEOUT = 0.2
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
N_BMS = 7
bms_cache = [None] * N_BMS
last_serial_ts = 0
clients = set()
serial_fd = None
serial_write_lock = asyncio.Lock()
arduino_fd = None
arduino_write_lock = asyncio.Lock()
last_heartbeat = 0.0
watchdog_triggered = False

# Fiziksel kopmayi (kablo cekilmesi) TCP seviyesinde en hizli ve en kesin
# sekilde tespit etmek icin: TCP_USER_TIMEOUT olmadan, karsi taraf ACK
# vermeyi kesse bile cekirdek varsayilan olarak dakikalarca sessizce
# yeniden denemeye devam edebilir - bu surece soket "acik" gorunmeye devam
# eder, tarayicinin JS'i HB gonderemese de. TCP_USER_TIMEOUT, cekirdege
# "bu surede ACK alamazsan baglantiyi hemen olu ilan et" der; boylece
# okuma/yazma (readexactly/drain) fiili kopmada gercekten hizla hataya
# duser. Eskiden bu ise ayri bir ICMP ping alt sureciyle (last_client_ip
# uzerinden) bakiliyordu; hem daha yavasti (600ms) hem de yeni baglanan
# HERHANGI bir istemcide last_client_ip'yi degistirip yanlis IP'yi izlemeye
# devam etme hatasi vardi. Bu ayarla o katmana gerek kalmiyor.
SOCKET_DEAD_TIMEOUT_MS = 300
KEEPALIVE_IDLE_S = 1
KEEPALIVE_INTERVAL_S = 1
KEEPALIVE_COUNT = 2

# Mod 1 - Otonom VFD profili.
# Her kademe 2 saniye surer; toplam 10 saniyenin sonunda motor durur.
AUTONOMOUS_PROFILE = (
    (28.0, 2.0),
    (56.0, 2.0),
    (84.0, 2.0),
    (112.0, 2.0),
    (140.0, 2.0),
)
SPEED_FREQUENCY_POINTS = (
    (0.0, 15.0),
    (5.0, 25.0),
    (10.0, 40.0),
    (20.0, 60.0),
    (30.0, 80.0),
    (40.0, 100.0),
    (50.0, 120.0),
    (60.0, 140.0),
)
autonomous_task = None
autonomous_direction = "forward"
autonomous_mode = 0

# BNO055 hiz kestirimi. Sensor arac uzerinde X ekseni ileri yone bakiyor.
# Ortam degiskenleriyle montaj yonu daha sonra kod degistirmeden ayarlanabilir:
#   IMU_FORWARD_AXIS=lx|ly|lz, IMU_FORWARD_SIGN=1|-1
IMU_FORWARD_AXIS = os.environ.get("IMU_FORWARD_AXIS", "lx").lower()
IMU_FORWARD_SIGN = float(os.environ.get("IMU_FORWARD_SIGN", "1"))
IMU_ACCEL_DEADBAND = 0.08       # m/s2
IMU_ACCEL_FILTER_TAU = 0.25     # saniye
IMU_CALIBRATION_SAMPLES = 30    # 10 Hz'de yaklasik 3 saniye
imu_speed_state = {
    "last_t": None,
    "bias_sum": 0.0,
    "bias_count": 0,
    "bias": 0.0,
    "filtered_accel": 0.0,
    "previous_accel": 0.0,
    "velocity_mps": 0.0,
    "distance_m": 0.0,
    "max_speed_kmh": 0.0,
}


def reset_imu_speed(recalibrate=False):
    """IMU hiz/yol kestirimini sifirlar.

    recalibrate=True ise arac hareketsiz kabul edilerek ileri eksen ofseti
    yeniden 3 saniye boyunca olculur.
    """
    imu_speed_state["last_t"] = None
    imu_speed_state["filtered_accel"] = 0.0
    imu_speed_state["previous_accel"] = 0.0
    imu_speed_state["velocity_mps"] = 0.0
    imu_speed_state["distance_m"] = 0.0
    imu_speed_state["max_speed_kmh"] = 0.0
    if recalibrate:
        imu_speed_state["bias_sum"] = 0.0
        imu_speed_state["bias_count"] = 0
        imu_speed_state["bias"] = 0.0
    print("[IMU-Speed] Sifirlandi%s" %
          (" ve ofset kalibrasyonu baslatildi" if recalibrate else ""))


def update_imu_speed(lx, ly, lz):
    """Yercekimsiz ileri ivmeyi entegre ederek anlik hiz/yol kestirir."""
    axes = {"lx": lx, "ly": ly, "lz": lz}
    raw_accel = float(axes.get(IMU_FORWARD_AXIS, lx)) * IMU_FORWARD_SIGN
    now = time.monotonic()
    last_t = imu_speed_state["last_t"]
    imu_speed_state["last_t"] = now

    # Ilk saniyelerde arac hareketsiz kabul edilerek sensor ofseti alinir.
    if imu_speed_state["bias_count"] < IMU_CALIBRATION_SAMPLES:
        imu_speed_state["bias_sum"] += raw_accel
        imu_speed_state["bias_count"] += 1
        imu_speed_state["bias"] = (
            imu_speed_state["bias_sum"] / imu_speed_state["bias_count"]
        )
        imu_speed_state["filtered_accel"] = 0.0
        imu_speed_state["previous_accel"] = 0.0
        return {
            "speed_mps": 0.0,
            "speed_kmh": 0.0,
            "signed_speed_mps": 0.0,
            "distance_m": 0.0,
            "max_speed_kmh": 0.0,
            "speed_accel": 0.0,
            "speed_axis": IMU_FORWARD_AXIS.upper(),
            "speed_calibrating": True,
        }

    if last_t is None:
        return {
            "speed_mps": round(abs(imu_speed_state["velocity_mps"]), 3),
            "speed_kmh": round(abs(imu_speed_state["velocity_mps"]) * 3.6, 2),
            "signed_speed_mps": round(imu_speed_state["velocity_mps"], 3),
            "distance_m": round(imu_speed_state["distance_m"], 2),
            "max_speed_kmh": round(imu_speed_state["max_speed_kmh"], 2),
            "speed_accel": 0.0,
            "speed_axis": IMU_FORWARD_AXIS.upper(),
            "speed_calibrating": False,
        }

    dt = now - last_t
    if dt <= 0.0 or dt > 0.5:
        imu_speed_state["previous_accel"] = 0.0
        dt = 0.0

    corrected = raw_accel - imu_speed_state["bias"]
    alpha = dt / (IMU_ACCEL_FILTER_TAU + dt) if dt > 0.0 else 0.0
    filtered = imu_speed_state["filtered_accel"]
    filtered += alpha * (corrected - filtered)
    if abs(filtered) < IMU_ACCEL_DEADBAND:
        filtered = 0.0
    imu_speed_state["filtered_accel"] = filtered

    old_velocity = imu_speed_state["velocity_mps"]
    avg_accel = (imu_speed_state["previous_accel"] + filtered) * 0.5
    new_velocity = old_velocity + avg_accel * dt

    # Yavaslama sirasinda sifirin diger tarafina gecip hayali ters hiz
    # olusmasini engelle; sifirdan gercek ters ivmelenmeye yine izin ver.
    if old_velocity > 0.0 and new_velocity < 0.0 and filtered < 0.0:
        new_velocity = 0.0
    elif old_velocity < 0.0 and new_velocity > 0.0 and filtered > 0.0:
        new_velocity = 0.0
    if abs(new_velocity) < 0.03 and filtered == 0.0:
        new_velocity = 0.0

    # Hatali sensorde arayuzu korumak icin fiziksel olmayan uclari sinirla.
    new_velocity = max(-55.56, min(55.56, new_velocity))  # +/- 200 km/h
    avg_speed = (abs(old_velocity) + abs(new_velocity)) * 0.5
    imu_speed_state["distance_m"] += avg_speed * dt
    imu_speed_state["velocity_mps"] = new_velocity
    imu_speed_state["previous_accel"] = filtered

    speed_kmh = abs(new_velocity) * 3.6
    if speed_kmh > imu_speed_state["max_speed_kmh"]:
        imu_speed_state["max_speed_kmh"] = speed_kmh

    # Arac gercekten sifira yakin ve sensor de sakin ise ofseti cok yavas
    # izleyerek sicaklik kaynakli kaymayi azalt.
    if abs(new_velocity) < 0.10 and abs(corrected) < 0.20:
        imu_speed_state["bias"] += 0.002 * corrected

    return {
        "speed_mps": round(abs(new_velocity), 3),
        "speed_kmh": round(speed_kmh, 2),
        "signed_speed_mps": round(new_velocity, 3),
        "distance_m": round(imu_speed_state["distance_m"], 2),
        "max_speed_kmh": round(imu_speed_state["max_speed_kmh"], 2),
        "speed_accel": round(filtered, 3),
        "speed_axis": IMU_FORWARD_AXIS.upper(),
        "speed_calibrating": False,
    }


async def restart_uart_bridge(reason):
    """Systemd'nin UART servisini temiz bir fd ile yeniden baslatmasini sagla."""
    print("[Serial] Bridge yeniden baslatiliyor: %s" % reason, flush=True)
    try:
        await broadcast({
            "type": "uart_reset",
            "data": {"status": "restarting", "reason": reason},
        })
        await asyncio.sleep(0.15)
    except Exception:
        pass
    # Unit Restart=always ile iki saniye icinde temiz bir surec baslatir.
    os._exit(75)


async def send_to_pi(text):
    global serial_fd
    if text.strip() == "MOTOR_STOP":
        reset_imu_speed(False)
    if serial_fd is None:
        return False
    line = (text.strip() + "\n").encode()
    async with serial_write_lock:
        try:
            os.write(serial_fd, line)
            return True
        except Exception as e:
            print("[Serial] Pi yazma hatasi: %s" % e, flush=True)
            # ttyTHS1 EIO verdiginde fd acik gorunse de sonraki butun yazmalar
            # basarisiz oluyor. Systemd ile temiz surec/fd acmak en guvenli
            # kurtarma yoludur.
            await restart_uart_bridge("serial_write_error")
            return False


def autonomous_is_active():
    return autonomous_task is not None and not autonomous_task.done()


async def broadcast_autonomous(active, direction=None, stage=0, frequency=0.0,
                               elapsed=0.0, remaining=0.0, reason="", mode=None,
                               speed_kmh=0.0):
    await broadcast({
        "type": "autonomous_mode",
        "data": {
            "active": bool(active),
            "mode": int(autonomous_mode if mode is None else mode),
            "direction": direction or autonomous_direction,
            "stage": int(stage),
            "frequency": float(frequency),
            "speed_kmh": round(float(speed_kmh), 1),
            "elapsed": round(float(elapsed), 1),
            "remaining": round(float(remaining), 1),
            "reason": reason,
        },
    })


async def autonomous_sequence(direction):
    """Secilen yonde 10 saniyelik VFD frekans profilini uygular."""
    global autonomous_task, autonomous_direction, autonomous_mode
    autonomous_direction = direction
    autonomous_mode = 1
    total_duration = sum(item[1] for item in AUTONOMOUS_PROFILE)
    started = time.monotonic()
    try:
        await send_to_pi(
            "MOTOR_DIR_FORWARD" if direction == "forward"
            else "MOTOR_DIR_REVERSE"
        )
        for index, (frequency, duration) in enumerate(AUTONOMOUS_PROFILE):
            await send_to_pi("MOTOR_FREQ:%g" % frequency)
            if index == 0:
                await send_to_pi("MOTOR_GO")
            stage_end = started + sum(
                item[1] for item in AUTONOMOUS_PROFILE[:index + 1]
            )
            while True:
                now = time.monotonic()
                elapsed = min(total_duration, now - started)
                remaining = max(0.0, total_duration - elapsed)
                await broadcast_autonomous(
                    True, direction, index + 1, frequency, elapsed, remaining,
                    mode=1
                )
                wait_time = stage_end - now
                if wait_time <= 0.0:
                    break
                await asyncio.sleep(min(0.2, wait_time))

        await send_to_pi("MOTOR_STOP")
        await broadcast_autonomous(
            False, direction, len(AUTONOMOUS_PROFILE), 0.0,
            total_duration, 0.0, "completed", mode=1
        )
        print("[Autonom] Profil tamamlandi, motor durduruldu")
    except asyncio.CancelledError:
        raise
    except Exception as e:
        print("[Autonom] Hata: %s" % e)
        await send_to_pi("MOTOR_STOP")
        await broadcast_autonomous(
            False, direction, 0, 0.0, 0.0, 0.0, "error"
        )
    finally:
        autonomous_task = None
        autonomous_mode = 0


def speed_to_frequency(speed_kmh):
    """BNO hizini kullanici noktalarina gore dogrusal Hz degerine cevirir."""
    speed = max(0.0, float(speed_kmh))
    if speed <= SPEED_FREQUENCY_POINTS[0][0]:
        return SPEED_FREQUENCY_POINTS[0][1]
    for index in range(1, len(SPEED_FREQUENCY_POINTS)):
        speed_hi, frequency_hi = SPEED_FREQUENCY_POINTS[index]
        speed_lo, frequency_lo = SPEED_FREQUENCY_POINTS[index - 1]
        if speed <= speed_hi:
            ratio = (speed - speed_lo) / (speed_hi - speed_lo)
            return frequency_lo + ratio * (frequency_hi - frequency_lo)
    return SPEED_FREQUENCY_POINTS[-1][1]


async def speed_frequency_sequence(direction):
    """Mod 2: BNO hizina gore VFD frekansini surekli ayarlar."""
    global autonomous_task, autonomous_direction, autonomous_mode
    autonomous_direction = direction
    autonomous_mode = 2
    last_frequency = None
    last_frequency_ts = 0.0
    started = time.monotonic()
    motor_started = False
    try:
        await send_to_pi(
            "MOTOR_DIR_FORWARD" if direction == "forward"
            else "MOTOR_DIR_REVERSE"
        )
        while True:
            now = time.monotonic()
            imu_timestamp = imu_speed_state["last_t"]
            if imu_timestamp is None or now - imu_timestamp > 0.7:
                await send_to_pi("MOTOR_STOP")
                await broadcast_autonomous(
                    False, direction, 0, 0.0, now - started, 0.0,
                    "imu_lost", mode=2
                )
                print("[Hiz-Modu] BNO verisi kesildi, motor durduruldu")
                return

            speed_kmh = abs(imu_speed_state["velocity_mps"]) * 3.6
            target_frequency = speed_to_frequency(speed_kmh)
            if (
                last_frequency is None
                or abs(target_frequency - last_frequency) >= 0.5
                or now - last_frequency_ts >= 1.0
            ):
                await send_to_pi("MOTOR_FREQ:%.1f" % target_frequency)
                last_frequency = target_frequency
                last_frequency_ts = now
            if not motor_started:
                await send_to_pi("MOTOR_GO")
                motor_started = True

            await broadcast_autonomous(
                True, direction, 0, target_frequency, now - started, 0.0,
                mode=2, speed_kmh=speed_kmh
            )
            await asyncio.sleep(0.2)
    except asyncio.CancelledError:
        raise
    except Exception as e:
        print("[Hiz-Modu] Hata: %s" % e)
        await send_to_pi("MOTOR_STOP")
        await broadcast_autonomous(
            False, direction, 0, 0.0, 0.0, 0.0, "error", mode=2
        )
    finally:
        autonomous_task = None
        autonomous_mode = 0


async def cancel_autonomous(reason="user", stop_motor=True):
    """Calisan otonom profili iptal eder ve istenirse VFD'yi durdurur."""
    global autonomous_task, autonomous_mode
    task = autonomous_task
    if task is None or task.done():
        autonomous_task = None
        return False
    cancelled_mode = autonomous_mode
    autonomous_task = None
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass
    if stop_motor:
        await send_to_pi("MOTOR_STOP")
    await broadcast_autonomous(
        False, autonomous_direction, 0, 0.0, 0.0, 0.0, reason,
        mode=cancelled_mode
    )
    autonomous_mode = 0
    print("[Autonom] Iptal edildi (%s), motor durduruldu" % reason)
    return True


async def start_autonomous(direction):
    """Varsa eski profili durdurur ve yeni bir Mod 1 profili baslatir."""
    global autonomous_task, autonomous_direction, autonomous_mode
    if direction not in ("forward", "reverse"):
        direction = "forward"
    if autonomous_is_active():
        await cancel_autonomous("restarted", True)
    autonomous_direction = direction
    autonomous_mode = 1
    autonomous_task = asyncio.ensure_future(autonomous_sequence(direction))
    print("[Autonom] Baslatildi, yon=%s" % direction)


async def start_speed_frequency_mode(direction):
    """Guvenlik ve BNO hazirsa Mod 2'yi baslatir."""
    global autonomous_task, autonomous_direction, autonomous_mode
    if direction not in ("forward", "reverse"):
        direction = "forward"
    if imu_speed_state["bias_count"] < IMU_CALIBRATION_SAMPLES:
        await broadcast_autonomous(
            False, direction, 0, 0.0, 0.0, 0.0,
            "imu_calibrating", mode=2
        )
        print("[Hiz-Modu] Baslatilmadi: BNO kalibrasyonu suruyor")
        return False
    if autonomous_is_active():
        await cancel_autonomous("restarted", True)
    autonomous_direction = direction
    autonomous_mode = 2
    autonomous_task = asyncio.ensure_future(speed_frequency_sequence(direction))
    print("[Hiz-Modu] Baslatildi, yon=%s" % direction)
    return True


async def log_cmd(target, text):
    print("[CMD->%s] %s" % (target, text), flush=True)
    await broadcast({
        "type": "cmd_log",
        "target": target,
        "command": text,
        "ts": time.time(),
    })


async def send_to_arduino(text, log=True):
    # KRITIK: seri porta yazma ONCE yapilir. log_cmd() dashboard'a broadcast
    # yapiyor ve olu/yaniti gelmeyen bir WS istemcisi varsa (ornegin PC'nin
    # etherneti koptuysa) bu broadcast, isletim sisteminin o soketi olu ilan
    # etmesini (10-30+ saniye surebilir) beklerken guvenlik komutunun
    # Arduino'ya ulasmasini geciktirebilirdi. Once yaz, log/broadcast'i arka
    # planda (beklenmeden) yap.
    global arduino_fd
    if arduino_fd is not None:
        line = (text.strip() + "\n").encode()
        async with arduino_write_lock:
            try:
                os.write(arduino_fd, line)
            except Exception as e:
                print("[Arduino] Yazma hatasi: %s" % e)
                # ttyTHS1/Pi hattindaki ile ayni sorun: fd bir kere EIO
                # verdiginde acik gorunse de sonraki TUM yazmalar sessizce
                # basarisiz olur ve okuma tarafi bunu fark etmeyebilir (cihaz
                # sokete gore degil dosyaya gore okunuyor). Fren komutlarinin
                # sonsuza kadar sessizce kaybolmasindansa temiz surec/fd ile
                # (systemd Restart=always) hemen kurtarmak daha guvenli.
                await restart_uart_bridge("arduino_write_error")
                return
    if log:
        asyncio.ensure_future(log_cmd("arduino", text))


def touch_heartbeat():
    global last_heartbeat, watchdog_triggered
    last_heartbeat = time.time()
    if watchdog_triggered:
        watchdog_triggered = False
        # NOT: eskiden burada "BUZZER_OFF" gonderiliyordu ama Arduino'da
        # boyle bir komut yok (buzzer tamamen brakeFront/brakeRear'dan
        # otomatik turetiliyor) - o cagri hicbir zaman calismiyordu.
        asyncio.ensure_future(broadcast({"type": "safety", "data": {"armed": True, "triggered": False, "reconnected": True}}))


async def trigger_emergency(reason):
    """Frenleri ve motoru guvenli (kilitli) duruma alir.

    Iki ayri yerden cagrilabilir: periyodik safety_watchdog() (heartbeat
    suresi HEARTBEAT_TIMEOUT'u astiginda) ve handle_client()'in kendisi
    - bir istemcinin baglantisi TAM O AN koptugunda, 20ms'lik periyodik
    kontrolu bile beklemeden. Hangisi once tetiklerse watchdog_triggered
    bayragiyla digerini engeller (cift tetiklemeyi onler).
    """
    global watchdog_triggered
    if watchdog_triggered:
        return
    watchdog_triggered = True
    print("[Safety] TETIKLENDI (%s): FRONT_ON + REAR_ON + MOTOR_STOP" % reason, flush=True)
    # KRITIK: fiili guvenlik komutlari ONCE, dashboard'a log/broadcast
    # SONRA (ve beklenmeden) - olu bir WS istemcisine yazmaya calisirken
    # burada takilip fren komutunu geciktirmesin.
    await send_to_arduino("FRONT_ON", log=False)
    await send_to_arduino("REAR_ON", log=False)
    await send_to_pi("MOTOR_STOP")
    await send_to_pi("MOTOR_STOP")
    await send_to_pi("MOTOR_STOP")
    asyncio.ensure_future(cancel_autonomous(reason, False))
    asyncio.ensure_future(log_cmd("system", "WATCHDOG TETIKLENDI (%s)" % reason))
    asyncio.ensure_future(log_cmd("arduino", "FRONT_ON"))
    asyncio.ensure_future(log_cmd("arduino", "REAR_ON"))
    asyncio.ensure_future(broadcast({"type": "safety", "data": {"armed": True, "triggered": True, "reason": reason}}))


async def safety_watchdog():
    """Yedek/periyodik kontrol: son heartbeat'ten bu yana HEARTBEAT_TIMEOUT
    gectiyse tetikler. Gercek fiziksel kopmalarda asil hizli tepki artik
    handle_client()'in baglanti koptugu ani yakalayip trigger_emergency()'i
    dogrudan cagirmasindan gelir (asagida); bu 20ms'lik dongu, o yolun
    kacirabilecegi durumlar icin guvence.
    """
    while True:
        await asyncio.sleep(0.02)
        if not watchdog_triggered and time.time() - last_heartbeat > HEARTBEAT_TIMEOUT:
            await trigger_emergency("heartbeat_kesildi")


# Arduino'nun kendi SAFE_HB watchdog'unu besler. Bu sinyal SADECE tarayicidan
# gelen HB tazeyken gonderilir - yani PC<->Jetson baglantisi ya da Jetson
# sureci kesilirse bu gonderim de otomatik olarak durur ve Arduino kendi
# SAFE_HB_TIMEOUT_MS suresi icinde bagimsiz olarak fren kilitler. Boylece
# Jetson<->Arduino seri hatti aninda calissa da calismasa da (USB kopmasi,
# port yeniden numaralanmasi, DMA hatasi vb.) fren garantiye alinir.
ARDUINO_SAFE_HB_INTERVAL = 0.1
# SAFE_ARM'i periyodik olarak yeniden gonderiyoruz: Arduino'nun kendi
# watchdog'u (Katman 3) bir kez tetiklendiginde (safeTripped=true) ya da
# kart elektriksel bir nedenle (OC ALARM/USB kopmasi vb.) resetlenip
# safeArmed=false'a dondugunde, eskiden BIR DAHA KENDI KENDINE asla
# yeniden silahlanmiyordu - sadece Jetson'in portu ilk actiginda tek
# seferlik gonderiliyordu. SAFE_ARM sadece izlemeyi acar/sayaci sifirlar,
# brakeFront/brakeRear'a hic dokunmaz - yani mevcut fren durumunu
# etkilemeden guvenle her zaman tekrar gonderilebilir.
ARDUINO_SAFE_ARM_INTERVAL = 2.0


async def arduino_safety_heartbeat():
    last_arm_sent = 0.0
    while True:
        await asyncio.sleep(ARDUINO_SAFE_HB_INTERVAL)
        now = time.time()
        if now - last_arm_sent >= ARDUINO_SAFE_ARM_INTERVAL:
            await send_to_arduino("SAFE_ARM", log=False)
            last_arm_sent = now
        if not watchdog_triggered and (now - last_heartbeat) < HEARTBEAT_TIMEOUT:
            await send_to_arduino("SAFE_HB", log=False)


def ws_accept_key(key):
    sha1 = hashlib.sha1((key + GUID).encode()).digest()
    return base64.b64encode(sha1).decode()


async def ws_handshake(reader, writer):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = await reader.read(2048)
        if not chunk:
            return False
        data += chunk
    text = data.decode(errors="ignore")
    headers = {}
    for line in text.split("\r\n")[1:]:
        if ": " in line:
            k, v = line.split(": ", 1)
            headers[k.strip().lower()] = v.strip()
    key = headers.get("sec-websocket-key")
    if not key:
        return False
    accept = ws_accept_key(key)
    resp = ("HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n" % accept)
    writer.write(resp.encode())
    await writer.drain()
    return True


def encode_text_frame(payload):
    data = payload.encode() if isinstance(payload, str) else payload
    n = len(data)
    if n <= 125:
        header = struct.pack("!BB", 0x81, n)
    elif n <= 0xFFFF:
        header = struct.pack("!BBH", 0x81, 126, n)
    else:
        header = struct.pack("!BBQ", 0x81, 127, n)
    return header + data


def encode_pong(payload=b""):
    n = len(payload)
    return struct.pack("!BB", 0x8A, n) + payload


async def read_frame(reader):
    hdr = await reader.readexactly(2)
    b1, b2 = hdr[0], hdr[1]
    opcode = b1 & 0x0F
    masked = bool(b2 & 0x80)
    length = b2 & 0x7F
    if length == 126:
        length = struct.unpack("!H", await reader.readexactly(2))[0]
    elif length == 127:
        length = struct.unpack("!Q", await reader.readexactly(8))[0]
    mask = await reader.readexactly(4) if masked else b"\x00\x00\x00\x00"
    payload = await reader.readexactly(length) if length else b""
    if masked:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return opcode, payload


# TCP baglantisi fiziksel olarak kopup temiz kapanmadiginda (ornegin
# ethernet kablosu cekilirse) isletim sistemi bu soketi olu ilan edene
# kadar 10-30+ saniye gecebilir. w.drain() bu sure boyunca beklerse,
# broadcast() o istemcide takilir ve ONU CAGIRAN HERKESI (safety_watchdog
# dahil) bloke eder. Bu yuzden her drain() sinirli sürede zaman asimina
# ugratilip olu istemci hemen elenir.
DRAIN_TIMEOUT = 0.3


async def send_json(writer, obj):
    try:
        writer.write(encode_text_frame(json.dumps(obj)))
        await asyncio.wait_for(writer.drain(), timeout=DRAIN_TIMEOUT)
    except Exception:
        pass


async def broadcast(obj):
    if isinstance(obj, str):
        msg = encode_text_frame(obj)
    else:
        msg = encode_text_frame(json.dumps(obj))
    dead = set()
    for w in list(clients):
        try:
            w.write(msg)
            await asyncio.wait_for(w.drain(), timeout=DRAIN_TIMEOUT)
        except Exception:
            dead.add(w)
    for w in dead:
        try:
            w.close()
        except Exception:
            pass
    clients.difference_update(dead)


async def broadcast_except(obj, exclude_writer):
    if isinstance(obj, str):
        msg = encode_text_frame(obj)
    else:
        msg = encode_text_frame(json.dumps(obj))
    dead = set()
    for w in list(clients):
        if w is exclude_writer:
            continue
        try:
            w.write(msg)
            await asyncio.wait_for(w.drain(), timeout=DRAIN_TIMEOUT)
        except Exception:
            dead.add(w)
    for w in dead:
        try:
            w.close()
        except Exception:
            pass
    clients.difference_update(dead)


def configure_socket_fast_failure(writer):
    """Kabul edilen istemci soketine TCP_USER_TIMEOUT + SO_KEEPALIVE
    uygular (bkz. dosya basindaki SOCKET_DEAD_TIMEOUT_MS aciklamasi).
    Boylece kablo fiziksel olarak cekildiginde okuma/yazma (readexactly/
    drain), isletim sisteminin varsayilan (cok daha yavas, dakikalarca
    surebilen) tespit suresini beklemeden hizla hataya duser.
    """
    sock = writer.get_extra_info("socket")
    if sock is None:
        return
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, KEEPALIVE_IDLE_S)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, KEEPALIVE_INTERVAL_S)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, KEEPALIVE_COUNT)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_USER_TIMEOUT, SOCKET_DEAD_TIMEOUT_MS)
    except (AttributeError, OSError) as e:
        print("[Socket] Hizli-kopma ayari uygulanamadi: %s" % e, flush=True)


async def handle_client(reader, writer):
    peer = writer.get_extra_info("peername")
    ok = await ws_handshake(reader, writer)
    if not ok:
        writer.close()
        return
    configure_socket_fast_failure(writer)
    print("[WS] Tarayici bagli: %s" % str(peer))
    clients.add(writer)
    known = [b for b in bms_cache if b is not None]
    if known:
        await send_json(writer, {"type": "bms", "data": known})
    try:
        writer.write(encode_text_frame("PI_CONNECTED"))
        await writer.drain()
    except Exception:
        pass
    try:
        while True:
            opcode, payload = await read_frame(reader)
            if opcode == 0x8:
                break
            elif opcode == 0x9:
                writer.write(encode_pong(payload))
                await writer.drain()
            elif opcode == 0x1:
                try:
                    text = payload.decode(errors="ignore").strip()
                except Exception:
                    text = ""
                if not text:
                    continue
                if text.startswith("{"):
                    print("[WS-DBG] JSON alindi: %s" % text[:80])
                if text == "UART_RESET":
                    if autonomous_is_active():
                        await cancel_autonomous("uart_reset", True)
                    else:
                        # Manuel reset kontrol hattini kisa sure kesecegi icin
                        # once motoru guvenli sekilde durdur.
                        await send_to_pi("MOTOR_STOP")
                    await asyncio.sleep(0.1)
                    await restart_uart_bridge("manual")
                elif text == "IMU_SPEED_RESET":
                    if autonomous_is_active() and autonomous_mode == 2:
                        await cancel_autonomous("imu_reset", True)
                    reset_imu_speed(True)
                    await broadcast({"type": "imu_speed_reset"})
                elif text.startswith("AUTONOM_START:"):
                    direction = text.split(":", 1)[1].lower()
                    await start_autonomous(direction)
                elif text.startswith("SPEED_MODE_START:"):
                    direction = text.split(":", 1)[1].lower()
                    await start_speed_frequency_mode(direction)
                elif text == "AUTONOM_STOP":
                    stopped = await cancel_autonomous("user_stop", True)
                    if not stopped:
                        await send_to_pi("MOTOR_STOP")
                        await broadcast_autonomous(
                            False, autonomous_direction, 0, 0.0,
                            0.0, 0.0, "user_stop"
                        )
                elif text.startswith("VFD_") or text.startswith("MOTOR_"):
                    if autonomous_is_active():
                        await cancel_autonomous("manual_command", True)
                    ok = await send_to_pi(text)
                    if text in ("MOTOR_DIR_FORWARD", "MOTOR_DIR_REVERSE"):
                        await send_json(writer, {
                            "type": "motor_dir_ack",
                            "command": text,
                            "ok": ok,
                        })
                elif text == "ARM" or text == "DISARM" or text == "HB":
                    # Koruma daima aktif; ARM/DISARM artik sadece HB gibi
                    # davranir (eski/onbellekteki istemcilerle uyumluluk icin).
                    touch_heartbeat()
                elif text == "PING":
                    try:
                        writer.write(encode_text_frame("PONG"))
                        await writer.drain()
                    except Exception:
                        pass
                elif text.startswith("{"):
                    try:
                        parsed = json.loads(text)
                        await broadcast_except(parsed, writer)
                    except Exception:
                        await send_to_arduino(text)
                else:
                    if text in (
                        "FRONT_ON", "REAR_ON", "EMERGENCY_STOP",
                        "FRONT_ABS_ON", "REAR_ABS_ON", "ALL_ABS_ON"
                    ) and autonomous_is_active():
                        await cancel_autonomous("brake", True)
                    touch_heartbeat()
                    await send_to_arduino(text)
    except (asyncio.IncompleteReadError, ConnectionResetError):
        pass
    except Exception as e:
        print("[WS] Hata: %s" % e)
    finally:
        clients.discard(writer)
        try:
            writer.close()
        except Exception:
            pass
        print("[WS] Tarayici ayrildi: %s" % str(peer))
        # Kopan baglanti taze heartbeat'in tek kaynagi olabilir; 20ms'lik
        # periyodik safety_watchdog() kontrolunu bile beklemeden hemen bak.
        if not watchdog_triggered and time.time() - last_heartbeat > HEARTBEAT_TIMEOUT:
            asyncio.ensure_future(trigger_emergency("istemci_baglantisi_koptu"))


def open_serial_fd(path, baud):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    baud_const = getattr(termios, "B%d" % baud)
    attrs[4] = baud_const
    attrs[5] = baud_const
    attrs[2] = (attrs[2] & ~termios.CSIZE) | termios.CS8
    attrs[2] |= termios.CLOCAL | termios.CREAD
    attrs[2] &= ~termios.PARENB
    attrs[2] &= ~termios.CSTOPB
    attrs[0] = 0
    attrs[1] = 0
    attrs[3] = 0
    cc = attrs[6]
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 5
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)
    return fd


MAX_LINE_BYTES = 800
MAX_LINE_AGE = 1.0


def blocking_readline(fd, buf, state, timeout_s=1.0):
    deadline = time.time() + timeout_s
    while b"\n" not in buf:
        now = time.time()
        if buf and state[0] is not None and (now - state[0]) > MAX_LINE_AGE:
            buf.clear()
            state[0] = None
        elif len(buf) > MAX_LINE_BYTES:
            buf.clear()
            state[0] = None
        if now > deadline:
            return None
        chunk = os.read(fd, 4096)
        if chunk:
            if not buf:
                state[0] = time.time()
            buf.extend(chunk)
    idx = buf.index(b"\n")
    line = bytes(buf[:idx])
    del buf[:idx + 1]
    state[0] = None
    return line


async def serial_reader():
    global bms_cache, last_serial_ts, serial_fd
    fd = None
    buf = bytearray()
    state = [None]
    loop = asyncio.get_event_loop()
    while True:
        try:
            if fd is None:
                fd = open_serial_fd(SERIAL_PORT, SERIAL_BAUD)
                serial_fd = fd
                buf = bytearray()
                state = [None]
                print("[Serial] Acildi: %s @ %d" % (SERIAL_PORT, SERIAL_BAUD))
            line = await loop.run_in_executor(None, blocking_readline, fd, buf, state, 1.0)
            if line is None:
                continue
            try:
                text = line.decode(errors="ignore").strip()
            except Exception:
                continue
            if not text:
                continue
            try:
                data = json.loads(text)
            except json.JSONDecodeError:
                continue
            if isinstance(data, dict) and "x" in data and "k" in data:
                idx = data.get("x")
                if isinstance(idx, int) and 0 <= idx < N_BMS:
                    bms_cache[idx] = data
                    last_serial_ts = time.time()
                    known = [b for b in bms_cache if b is not None]
                    await broadcast({"type": "bms", "data": known})
            elif isinstance(data, dict) and data.get("type") in ("vfd_data", "vfd_write_result", "motor"):
                await broadcast(data)
        except Exception as e:
            print("[Serial] Hata: %s - 3sn sonra tekrar" % e)
            try:
                if fd is not None:
                    os.close(fd)
            except Exception:
                pass
            fd = None
            serial_fd = None
            await asyncio.sleep(3)


def parse_arduino_line(line):
    line = line.strip()
    if not line:
        return None
    if line.startswith("TEMP:"):
        try:
            return {"type": "temp", "data": {"value": float(line[5:].strip())}}
        except ValueError:
            return None
    if line.startswith("OMRON:"):
        try:
            return {"type": "omron", "data": {"stripe": int(line[6:].strip())}}
        except ValueError:
            return None
    if line.startswith("STATUS:"):
        try:
            return {"type": "status", "data": json.loads(line[7:].strip())}
        except (ValueError, json.JSONDecodeError):
            return None
    if line.startswith("EVENT:"):
        return {"type": "arduino_event", "event": line}
    if line == "PONG":
        return None
    print("[Arduino] %s" % line)
    return None


async def arduino_reader():
    global arduino_fd
    fd = None
    buf = bytearray()
    state = [None]
    loop = asyncio.get_event_loop()
    while True:
        try:
            if fd is None:
                fd = open_serial_fd(ARDUINO_PORT, ARDUINO_BAUD)
                arduino_fd = fd
                buf = bytearray()
                state = [None]
                print("[Arduino] Acildi: %s @ %d" % (ARDUINO_PORT, ARDUINO_BAUD))
                # Koruma 7/24 aktif: tarayici hic baglanmasa bile Arduino'nun
                # kendi bagimsiz SAFE_HB izlemesini hemen devreye al.
                await send_to_arduino("SAFE_ARM", log=False)
            line = await loop.run_in_executor(None, blocking_readline, fd, buf, state, 1.0)
            if line is None:
                continue
            try:
                text = line.decode("utf-8", errors="replace").strip()
            except Exception:
                continue
            msg = parse_arduino_line(text)
            if msg:
                await broadcast(msg)
        except FileNotFoundError:
            arduino_fd = None
            await asyncio.sleep(3)
        except Exception as e:
            print("[Arduino] Hata: %s - 3sn sonra tekrar" % e)
            try:
                if fd is not None:
                    os.close(fd)
            except Exception:
                pass
            fd = None
            arduino_fd = None
            await asyncio.sleep(3)


async def conn_status_broadcaster():
    while True:
        await asyncio.sleep(2)
        arduino_ok = arduino_fd is not None
        await broadcast({"type": "status", "data": {"arduino": arduino_ok}})


async def imu_reader():
    import fcntl
    I2C_SLAVE = 0x0703
    fd = None
    while True:
        if fd is None:
            try:
                fd = os.open("/dev/i2c-1", os.O_RDWR)
                fcntl.ioctl(fd, I2C_SLAVE, 0x28)
                os.write(fd, bytes([0x00]))
                cid = os.read(fd, 1)[0]
                if cid != 0xA0:
                    os.close(fd)
                    fd = None
                    await asyncio.sleep(5)
                    continue
                os.write(fd, bytes([0x3D, 0x00]))
                await asyncio.sleep(0.05)
                os.write(fd, bytes([0x3E, 0x00]))
                await asyncio.sleep(0.02)
                os.write(fd, bytes([0x07, 0x00]))
                await asyncio.sleep(0.02)
                os.write(fd, bytes([0x3F, 0x00]))
                await asyncio.sleep(0.02)
                os.write(fd, bytes([0x3D, 0x0C]))
                await asyncio.sleep(0.5)
                reset_imu_speed(True)
                print("[IMU] BNO055 NDOF modunda baslatildi")
            except Exception as e:
                print("[IMU] Init hatasi: %s" % e)
                if fd is not None:
                    try:
                        os.close(fd)
                    except Exception:
                        pass
                fd = None
                await asyncio.sleep(5)
                continue
        try:
            # Euler acilari: 0x1A (6 byte) heading/roll/pitch, /16 = derece
            os.write(fd, bytes([0x1A]))
            eul = os.read(fd, 6)
            # Ivme: 0x08 (6 byte)
            os.write(fd, bytes([0x08]))
            acc = os.read(fd, 6)
            # Linear ivme (yercekimsiz): 0x28 (6 byte)
            os.write(fd, bytes([0x28]))
            lin = os.read(fd, 6)
            # Gravity: 0x2E (6 byte)
            os.write(fd, bytes([0x2E]))
            grv = os.read(fd, 6)
            # Gyro: 0x14 (6 byte)
            os.write(fd, bytes([0x14]))
            gyr = os.read(fd, 6)
            # Manyetometre: 0x0E (6 byte)
            os.write(fd, bytes([0x0E]))
            mag = os.read(fd, 6)
            # Kalibrasyon: 0x35 (1 byte)
            os.write(fd, bytes([0x35]))
            cal = os.read(fd, 1)[0]
            # Sicaklik: 0x34 (1 byte)
            os.write(fd, bytes([0x34]))
            tmp = os.read(fd, 1)
            temp = struct.unpack_from("b", tmp, 0)[0]
            s = struct.unpack_from
            data = {
                "heading": round(s("<h", eul, 0)[0] / 16.0, 1),
                "roll": round(s("<h", eul, 2)[0] / 16.0, 1),
                "pitch": round(s("<h", eul, 4)[0] / 16.0, 1),
                "ax": round(s("<h", acc, 0)[0] / 100.0, 2),
                "ay": round(s("<h", acc, 2)[0] / 100.0, 2),
                "az": round(s("<h", acc, 4)[0] / 100.0, 2),
                "lx": round(s("<h", lin, 0)[0] / 100.0, 2),
                "ly": round(s("<h", lin, 2)[0] / 100.0, 2),
                "lz": round(s("<h", lin, 4)[0] / 100.0, 2),
                "grx": round(s("<h", grv, 0)[0] / 100.0, 2),
                "gry": round(s("<h", grv, 2)[0] / 100.0, 2),
                "grz": round(s("<h", grv, 4)[0] / 100.0, 2),
                "gx": round(s("<h", gyr, 0)[0] / 16.0, 1),
                "gy": round(s("<h", gyr, 2)[0] / 16.0, 1),
                "gz": round(s("<h", gyr, 4)[0] / 16.0, 1),
                "mx": round(s("<h", mag, 0)[0] / 16.0, 1),
                "my": round(s("<h", mag, 2)[0] / 16.0, 1),
                "mz": round(s("<h", mag, 4)[0] / 16.0, 1),
                "cal_sys": (cal >> 6) & 3,
                "cal_gyr": (cal >> 4) & 3,
                "cal_acc": (cal >> 2) & 3,
                "cal_mag": cal & 3,
                "temp": temp,
            }
            data.update(update_imu_speed(data["lx"], data["ly"], data["lz"]))
            await broadcast({"type": "imu", "data": data})
            await asyncio.sleep(0.1)
        except OSError as e:
            print("[IMU] Okuma hatasi: %s" % e)
            try:
                os.close(fd)
            except Exception:
                pass
            fd = None
            await asyncio.sleep(3)
        except Exception as e:
            print("[IMU] Hata: %s" % e)
            await asyncio.sleep(1)


async def main():
    srv = await asyncio.start_server(handle_client, WS_HOST, WS_PORT)
    print("[WS] Dinleniyor: ws://%s:%d" % (WS_HOST, WS_PORT))
    asyncio.ensure_future(arduino_reader())
    asyncio.ensure_future(safety_watchdog())
    asyncio.ensure_future(arduino_safety_heartbeat())
    asyncio.ensure_future(conn_status_broadcaster())
    asyncio.ensure_future(imu_reader())
    await serial_reader()


if __name__ == "__main__":
    loop = asyncio.get_event_loop()
    try:
        loop.run_until_complete(main())
    except KeyboardInterrupt:
        print("\n[Bridge] Kapatiliyor.")
