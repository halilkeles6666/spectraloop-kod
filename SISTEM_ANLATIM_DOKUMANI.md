# Spectraloop Kapsül Kontrol Sistemi — Teknik Anlatım Dokümanı

Bu doküman, sistemi başka birine (veya başka bir sohbete) anlatabilmen için hazırlandı.
Mimariyi, her dosyanın görevini, nerede durduğunu ve tam kaynak kodlarını içeriyor.

---

## 1) Genel Mimari — Sistemde Kim Kiminle Konuşuyor

```
[PC / Bilgisayar]
   |  Ethernet
   v
[Ağ Erişim Modülü] (switch/router, DHCP kapalı, statik IP'lerle çalışıyor)
   |  Ethernet
   v
[NVIDIA Jetson]  <-- "Jetson" (arayüz + güvenlik beyni)
   |  \
   |   \___ UART (ttyTHS1, Jetson tarafı) ______________
   |                                                     |
   | USB (ttyUSB, CH340 adaptör)                         v
   v                                              [Raspberry Pi]  <-- "Raspberry"
[Arduino Uno]                                     (VFD/motor kontrolü)
(fren, sıcaklık,                                        |
 şerit sayacı,                                          | RS485 / Modbus RTU (ttyAMA3)
 güvenlik röleleri)                                      v
                                                   [Delta MS300 VFD] -> Motor
```

- **PC**: Kullanıcının tarayıcısı (dashboard) burada açılır. `http://192.168.9.101:3000/` adresinden Jetson'a bağlanır.
- **Ağ Erişim Modülü**: PC ile Jetson arasındaki switch/medya dönüştürücü. Sadece Ethernet trafiğini taşır.
- **Jetson**: Hem web arayüzünü (dashboard) HTTP ile sunar hem de bir WebSocket sunucusu çalıştırır. Arduino'ya USB üzerinden, Raspberry Pi'ye ise dahili UART (ttyTHS1) üzerinden bağlıdır. Sistemin "trafik polisi" ve güvenlik beynidir.
- **Arduino Uno**: Fren valfleri, buzzer, flaşör, stop lambası, kontaktör, SSR röleleri, DS18B20 sıcaklık sensörü ve Omron şerit sayacını doğrudan yönetir. Jetson'dan gelen metin komutlarıyla çalışır.
- **Raspberry Pi**: Jetson'dan UART üzerinden gelen komutları alıp Delta MS300 marka VFD'ye (motor sürücü) Modbus RTU (RS485) protokolüyle iletir. Motorun yönünü, frekansını, çalıştır/durdur durumunu kontrol eder.
- **Delta MS300 VFD**: Traksiyon motorunu fiilen süren frekans invertörü (motor sürücü).

> Not: Jetson'a doğrudan SSH ile bağlanıp dosyaları/servisleri inceledim (bu dokümandaki Jetson ve Arduino bilgileri canlı sistemden doğrulandı). Raspberry Pi'ye doğrudan erişimim olmadı; oradaki bilgiler `vfd_modbus.py` kaynak koduna ve Jetson tarafındaki protokole dayanıyor.

---

## 2) Dosyaların Nerede Olduğu

### PC üzerinde (kaynak kod / geliştirme kopyası)
`C:\Users\keles\OneDrive\Masaüstü\Spectraloop_Kod\`
| Dosya | Ne işe yarar |
|---|---|
| `uart_ws_bridge.py` | Jetson üzerinde çalışan ana Python programı (WebSocket sunucu + güvenlik mantığı) |
| `vfd_modbus.py` | Raspberry Pi üzerinde çalışan, VFD'yi Modbus ile konuşturan modül |
| `spectraloop_arduino_final.ino` | Arduino Uno firmware kaynak kodu |
| `index.html` | Ana dashboard arayüzü (Jetson'daki canlı kopyayla birebir aynı tutuluyor) |
| `bundle.js` | Kullanılmıyor / eski derleme kalıntısı (canlı sayfa bunu yüklemiyor) |

### Jetson üzerinde (canlı/çalışan sistem) — IP: `192.168.9.101`, kullanıcı: `jetson`
| Yol | Ne işe yarar |
|---|---|
| `/home/jetson/reflektorlu-similasyon/uart_ws_bridge.py` | **Çalışan** WebSocket köprüsü (root:root, 755) |
| `/home/jetson/spectraloop-ui/dist/index.html` | **Çalışan** ana dashboard sayfası |
| `/home/jetson/spectraloop-ui/dist/vfd.html` | VFD parametre ekranı |
| `/home/jetson/spectraloop-ui/dist/bms.html` | Batarya (BMS) izleme ekranı |
| `/home/jetson/spectraloop-ui/dist/pintest.html` | Pin/röle test ekranı |
| `/home/jetson/spectraloop-ui/http_server_ipcam.py` | Dashboard'u HTTP ile servis eden basit sunucu (port 3000) |
| `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` | Arduino'nun **sabit** USB seri yolu (ttyUSB0/1 numarası değişse de bu sabit kalır) |
| `/dev/ttyTHS1` | Jetson'un Raspberry Pi'ye bağlı olduğu dahili UART portu |

### systemd servisleri (Jetson, `sudo systemctl status <isim>` ile görülür)
| Servis | Görev |
|---|---|
| `uart-ws-bridge.service` | `uart_ws_bridge.py`'yi çalıştırır, çöktüğünde/hata verdiğinde 2 saniyede otomatik yeniden başlatır (`Restart=always`) |
| `spectraloop-http.service` | Dashboard'u `http://192.168.9.101:3000/` üzerinden servis eder |
| `kapsul-eth0.service` | Açılışta Jetson'un `eth0` arayüzüne sabit IP (192.168.9.101/24) verir |

---

## 3) Arduino (Uno) — `spectraloop_arduino_final.ino`

### Görevi
Fren valfleri, güvenlik röleleri, buzzer/stop lambası, sıcaklık ve şerit sensörünü **doğrudan** yönetir. Motor bu kartta değildir — motor tamamen Raspberry Pi → VFD hattından kontrol edilir.

### Pin Haritası
| Pin | Görev |
|---|---|
| D2 | DS18B20 sıcaklık sensörü |
| D3 | Omron (şerit/palet) sensörü — kesme (interrupt) ile sayılır |
| D4 | Buzzer rölesi |
| D7 | Kontaktör rölesi |
| D8 | SSR rölesi |
| D9 | Ön fren (valf) rölesi |
| D10 | Arka fren (valf) rölesi |
| D11 | Flaşör rölesi (şu an devre dışı bırakılmış) |
| D12 | Stop lambası rölesi |
| A2, A3 | Sensörlere sürekli 5V besleme çıkışı |

Röle mantığı **aktif-LOW**: `LOW = röle kapalı = yük ON`, `HIGH = röle açık = yük OFF`. (Flaşör/SSR/Kontaktör bunun tersine aktif-HIGH bağlı.)

### Komut Protokolü (Jetson → Arduino, seri, 115200 baud, satır sonu `\n`)
| Komut | Etki |
|---|---|
| `FRONT_ON` / `FRONT_OFF` | Ön fren valfini aç/kapat |
| `REAR_ON` / `REAR_OFF` | Arka fren valfini aç/kapat |
| `FRONT_ABS_ON/OFF`, `REAR_ABS_ON/OFF` | Aynı fren röleleriyle eşleşir |
| `CONTACTOR_ON/OFF` | Kontaktör |
| `SSR_ON/OFF` | SSR |
| `STOP_LIGHT_ON/OFF` | Stop lambası |
| `GT` | Sıcaklık oku → `TEMP:<değer>` |
| `GO` | Şerit sayacı oku → `OMRON:<sayı>` |
| `GA` | Tüm durumu JSON olarak oku → `STATUS:{...}` |
| `SAFE_ARM` | Kendi bağımsız güvenlik izlemesini başlat |
| `SAFE_DISARM` | Güvenlik izlemesini durdur |
| `SAFE_HB` | "Hâlâ buradayım" nabzı — sayaç sıfırlanır |

> **Not:** `BUZZER_ON` / `BUZZER_OFF` diye bir komut **yoktur**. Buzzer tamamen otomatik: **hem ön hem arka fren aynı anda aktifse çalar**, ikisinden biri bile serbest kalırsa **otomatik susar** (bkz. `updateAlarmFromBrakes()`). Stop lambası ise **herhangi bir** fren aktifse yanar.

### Güvenlik Watchdog Mantığı (Arduino'nun kendi, bağımsız katmanı)
Jetson, güvenlik aktifken her **100ms**'de bir `SAFE_HB` gönderir. Bu sinyal Arduino'nun **kendi** gözetimidir — Jetson'un `FRONT_ON`/`REAR_ON` göndermesinden tamamen bağımsız çalışır. Jetson süreci çökerse, USB hattı kopar/tıkanırsa veya Jetson'un PC ile bağlantısı kesilip `SAFE_HB` göndermeyi bırakırsa — Arduino, **400ms** (`SAFE_HB_TIMEOUT_MS`) boyunca hiç `SAFE_HB` almazsa ön+arka freni **kendi kararıyla** devreye alır. Bu, dashboard/Jetson tarafında ne olursa olsun çalışan **son savunma hattıdır**.

### Tam Kaynak Kod

```cpp
/*
 * Spectraloop - Arac Kontrol Sistemi TAM KOD (Arduino Uno)
 * -----------------------------------------------------------
 * Pin haritasi:
 *   D2  -> DS18B20 sicaklik sensoru (OneWire, 4.7k pull-up gerekir)
 *   D3  -> Omron (serit/pals) sensoru - INT1 kesmesi ile sayilir
 *   D4  -> Buzzer rolesi IN
 *   D7  -> Kontaktor rolesi IN
 *   D8  -> SSR rolesi IN
 *   D9  -> On fren (valf) rolesi IN
 *   D10 -> Arka fren (valf) rolesi IN
 *   D11 -> Flasor rolesi IN
 *   D12 -> Stop lambasi rolesi IN
 *   A2  -> 5V besleme cikisi (sensor gucu, surekli HIGH)
 *   A3  -> 5V besleme cikisi (sensor gucu, surekli HIGH)
 *
 * ONEMLI - Fren/Alarm mantigi:
 *   "EMERGENCY_STOP" / "RELEASE" gibi ayri bir acil-stop komutu YOKTUR.
 *   Flasor ve buzzer, HER IKI FREN DE (on+arka) aktif oldugunda OTOMATIK
 *   yanar/oter; ikisinden biri bile kapanirsa OTOMATIK soner/susar.
 *   Bu mantik Arduino'nun kendi icinde (updateAlarmFromBrakes) calisir,
 *   dashboard'un ayrica FLASHER_ON/BUZZER_ON gondermesine gerek yoktur.
 *
 * Motor bu Arduino'da degil, Pi -> VFD (Modbus) uzerinden kontrol edilir.
 *
 * Komut protokolu:
 *   FRONT_ON / FRONT_OFF         -> on fren valfi
 *   REAR_ON  / REAR_OFF          -> arka fren valfi
 *   FRONT_ABS_ON / FRONT_ABS_OFF -> on fren ile ayni role
 *   REAR_ABS_ON  / REAR_ABS_OFF  -> arka fren ile ayni role
 *   CONTACTOR_ON / CONTACTOR_OFF -> kontaktor
 *   SSR_ON / SSR_OFF             -> SSR
 *   STOP_LIGHT_ON / STOP_LIGHT_OFF -> stop lambasi
 *   GT  -> sicaklik oku -> "TEMP:<deger>"
 *   GO  -> omron sayaci oku -> "OMRON:<sayi>"
 *   GA  -> tum durum -> "STATUS:{...}" (JSON)
 *
 *   Bilinmeyen komut -> "ERR:cmd"
 *
 * GUVENLIK WATCHDOG (Jetson <-> Arduino hat kopmasina karsi):
 *   Jetson, arayuzdeki guvenlik switchi acikken her 100ms'de bir SAFE_HB
 *   gonderir. Bu Arduino'nun KENDI GOZETIMIDIR - Jetson'in FRONT_ON/REAR_ON
 *   komutu gonderip gondermemesinden BAGIMSIZDIR. Jetson sureci coksa, USB
 *   seri hatti koparsa/tikanirsa veya Jetson'in PC ile baglantisi kesilip
 *   Jetson SAFE_HB gondermeyi biraktiysa - Arduino SAFE_HB_TIMEOUT_MS suresi
 *   boyunca hicbir SAFE_HB almazsa ON+ARKA freni KENDISI devreye alir.
 *   Bu, dashboard/Jetson tarafinda ne olursa olsun calisan son savunma hattidir.
 *   SAFE_ARM  -> guvenlik izlemeyi baslat (switch acildi)
 *   SAFE_DISARM -> guvenlik izlemeyi durdur (switch kapandi)
 *   SAFE_HB   -> "hala buradayim" sinyali, sayaci sifirlar
 *
 * Role mantigi (standart Arduino role karti = AKTIF-LOW):
 *   LOW  = role kapali = yuk ON
 *   HIGH = role acik   = yuk OFF
 *
 * STATUS raporu her 500ms'de bir otomatik gonderilir.
 */

#include <OneWire.h>
#include <DallasTemperature.h>

// ── Pin tanimlari ─────────────────────────────────────────────────────────────
const int PIN_TEMP        = 2;
const int PIN_OMRON       = 3;
const int PIN_BUZZER      = 4;
const int PIN_CONTACTOR   = 7;
const int PIN_SSR         = 8;
const int PIN_RELAY_FRONT = 9;
const int PIN_RELAY_REAR  = 10;
const int PIN_FLASHER     = 11;
const int PIN_STOP_LIGHT  = 12;
const int PIN_5V_A2       = A2;
const int PIN_5V_A3       = A3;

const int RELAY_ON  = LOW;
const int RELAY_OFF = HIGH;

// ── Durum degiskenleri ────────────────────────────────────────────────────────
bool brakeFront  = false;
bool brakeRear   = false;
bool buzzerOn    = false;
bool flasherOn   = false;
bool stopLightOn = false;
bool contactorOn = false;
bool ssrOn       = false;

// ── Guvenlik watchdog (Jetson<->Arduino hatti) ──────────────────────────────
bool safeArmed   = false;
bool safeTripped = false;
unsigned long lastSafeHb = 0;
const unsigned long SAFE_HB_TIMEOUT_MS = 400UL;

// ── Omron (serit) sayaci ──────────────────────────────────────────────────────
volatile unsigned long omronCount = 0;
unsigned long lastOmronMillis = 0;
const unsigned long OMRON_DEBOUNCE_MS = 5;

void omronISR() {
    unsigned long now = millis();
    if (now - lastOmronMillis >= OMRON_DEBOUNCE_MS) {
        omronCount++;
        lastOmronMillis = now;
    }
}

// ── Zamanlayicilar ────────────────────────────────────────────────────────────
unsigned long lastFlashTime = 0;
bool          flashState    = false;
const unsigned long FLASH_INTERVAL = 500UL;

unsigned long lastStatusReport = 0;
const unsigned long STATUS_REPORT_INTERVAL = 500UL;

String inputBuffer = "";

// ── DS18B20 sicaklik sensoru (non-blocking) ──────────────────────────────────
OneWire oneWire(PIN_TEMP);
DallasTemperature dsSensor(&oneWire);
float lastTempReading = -999.0f;
bool tempConversionPending = false;
unsigned long tempConversionStart = 0;
const unsigned long TEMP_CONVERSION_MS = 750UL;

void updateTemperatureNonBlocking() {
    unsigned long now = millis();
    if (!tempConversionPending) {
        dsSensor.requestTemperatures();
        tempConversionStart = now;
        tempConversionPending = true;
    } else if (now - tempConversionStart >= TEMP_CONVERSION_MS) {
        float t = dsSensor.getTempCByIndex(0);
        if (t != DEVICE_DISCONNECTED_C) {
            lastTempReading = t;
        }
        tempConversionPending = false;
    }
}

float readTemperature() {
    return lastTempReading;
}

// ── Frenlere gore flasor/buzzer/stop lambasi durumunu guncelle ───────────────
void updateAlarmFromBrakes() {
    bool bothOn = brakeFront && brakeRear;
    bool anyOn  = brakeFront || brakeRear;

    if (bothOn && !buzzerOn) {
        buzzerOn = true;
        digitalWrite(PIN_BUZZER, RELAY_ON);
    } else if (!bothOn && buzzerOn) {
        buzzerOn = false;
        digitalWrite(PIN_BUZZER, RELAY_OFF);
    }

    if (anyOn && !stopLightOn) {
        stopLightOn = true;
        digitalWrite(PIN_STOP_LIGHT, RELAY_ON);
    } else if (!anyOn && stopLightOn) {
        stopLightOn = false;
        digitalWrite(PIN_STOP_LIGHT, RELAY_OFF);
    }
}

void checkSafeWatchdog() {
    if (safeArmed && !safeTripped) {
        if (millis() - lastSafeHb > SAFE_HB_TIMEOUT_MS) {
            safeTripped = true;
            digitalWrite(PIN_RELAY_FRONT, RELAY_ON);
            digitalWrite(PIN_RELAY_REAR, RELAY_ON);
            brakeFront = true;
            brakeRear  = true;
            updateAlarmFromBrakes();
            Serial.println("EVENT:SAFE_WATCHDOG_TRIPPED");
        }
    }
}

void reportStatus() {
    Serial.print("STATUS:{\"temp\":");
    Serial.print(readTemperature(), 1);
    Serial.print(",\"omron\":");
    Serial.print(omronCount);
    Serial.print(",\"brake_f\":");
    Serial.print(brakeFront ? "true" : "false");
    Serial.print(",\"brake_r\":");
    Serial.print(brakeRear ? "true" : "false");
    Serial.print(",\"contactor\":");
    Serial.print(contactorOn ? "true" : "false");
    Serial.print(",\"ssr\":");
    Serial.print(ssrOn ? "true" : "false");
    Serial.print(",\"flasher\":");
    Serial.print(flasherOn ? "true" : "false");
    Serial.print(",\"buzzer\":");
    Serial.print(buzzerOn ? "true" : "false");
    Serial.print(",\"stop_light\":");
    Serial.print(stopLightOn ? "true" : "false");
    Serial.print(",\"a2_5v\":");
    Serial.print(digitalRead(PIN_5V_A2) == HIGH ? "true" : "false");
    Serial.print(",\"a3_5v\":");
    Serial.print(digitalRead(PIN_5V_A3) == HIGH ? "true" : "false");
    Serial.print(",\"safe_armed\":");
    Serial.print(safeArmed ? "true" : "false");
    Serial.print(",\"safe_tripped\":");
    Serial.print(safeTripped ? "true" : "false");
    Serial.println("}");
}

void handleCommand(String line) {

    if (line == "FRONT_ON" || line == "FRONT_ABS_ON") {
        digitalWrite(PIN_RELAY_FRONT, RELAY_ON);
        brakeFront = true;
        updateAlarmFromBrakes();
        Serial.print("OK:"); Serial.println(line);
        return;
    }
    if (line == "FRONT_OFF" || line == "FRONT_ABS_OFF") {
        digitalWrite(PIN_RELAY_FRONT, RELAY_OFF);
        brakeFront = false;
        updateAlarmFromBrakes();
        Serial.print("OK:"); Serial.println(line);
        return;
    }
    if (line == "REAR_ON" || line == "REAR_ABS_ON") {
        digitalWrite(PIN_RELAY_REAR, RELAY_ON);
        brakeRear = true;
        updateAlarmFromBrakes();
        Serial.print("OK:"); Serial.println(line);
        return;
    }
    if (line == "REAR_OFF" || line == "REAR_ABS_OFF") {
        digitalWrite(PIN_RELAY_REAR, RELAY_OFF);
        brakeRear = false;
        updateAlarmFromBrakes();
        Serial.print("OK:"); Serial.println(line);
        return;
    }

    if (line == "CONTACTOR_ON") {
        digitalWrite(PIN_CONTACTOR, HIGH);
        contactorOn = true;
        Serial.println("OK:CONTACTOR_ON");
        return;
    }
    if (line == "CONTACTOR_OFF") {
        digitalWrite(PIN_CONTACTOR, LOW);
        contactorOn = false;
        Serial.println("OK:CONTACTOR_OFF");
        return;
    }

    if (line == "SSR_ON") {
        digitalWrite(PIN_SSR, HIGH);
        ssrOn = true;
        Serial.println("OK:SSR_ON");
        return;
    }
    if (line == "SSR_OFF") {
        digitalWrite(PIN_SSR, LOW);
        ssrOn = false;
        Serial.println("OK:SSR_OFF");
        return;
    }

    if (line == "STOP_LIGHT_ON") {
        digitalWrite(PIN_STOP_LIGHT, RELAY_ON);
        stopLightOn = true;
        Serial.println("OK:STOP_LIGHT_ON");
        return;
    }
    if (line == "STOP_LIGHT_OFF") {
        digitalWrite(PIN_STOP_LIGHT, RELAY_OFF);
        stopLightOn = false;
        Serial.println("OK:STOP_LIGHT_OFF");
        return;
    }

    if (line == "GT") {
        Serial.print("TEMP:");
        Serial.println(readTemperature(), 1);
        return;
    }
    if (line == "GO") {
        Serial.print("OMRON:");
        Serial.println(omronCount);
        return;
    }
    if (line == "GA") {
        reportStatus();
        return;
    }

    if (line == "SAFE_ARM") {
        safeArmed = true;
        safeTripped = false;
        lastSafeHb = millis();
        Serial.println("OK:SAFE_ARM");
        return;
    }
    if (line == "SAFE_DISARM") {
        safeArmed = false;
        safeTripped = false;
        Serial.println("OK:SAFE_DISARM");
        return;
    }
    if (line == "SAFE_HB") {
        lastSafeHb = millis();
        return;
    }

    Serial.print("ERR:"); Serial.println(line);
}

void setup() {
    Serial.begin(115200);

    pinMode(PIN_OMRON, INPUT_PULLUP);
    dsSensor.begin();
    dsSensor.setWaitForConversion(false);

    int outputs[] = {PIN_BUZZER,
                      PIN_RELAY_FRONT, PIN_RELAY_REAR, PIN_STOP_LIGHT};
    for (int i = 0; i < 4; i++) {
        pinMode(outputs[i], OUTPUT);
        digitalWrite(outputs[i], RELAY_OFF);
    }

    pinMode(PIN_FLASHER, OUTPUT);
    digitalWrite(PIN_FLASHER, LOW);
    pinMode(PIN_CONTACTOR, OUTPUT);
    digitalWrite(PIN_CONTACTOR, LOW);
    pinMode(PIN_SSR, OUTPUT);
    digitalWrite(PIN_SSR, LOW);

    pinMode(PIN_5V_A2, OUTPUT);
    pinMode(PIN_5V_A3, OUTPUT);
    digitalWrite(PIN_5V_A2, HIGH);
    digitalWrite(PIN_5V_A3, HIGH);

    attachInterrupt(digitalPinToInterrupt(PIN_OMRON), omronISR, FALLING);

    Serial.println("READY:SPECTRALOOP");
}

void loop() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n') {
            inputBuffer.trim();
            if (inputBuffer.length() > 0) {
                handleCommand(inputBuffer);
                inputBuffer = "";
            }
        } else if (c != '\r') {
            inputBuffer += c;
        }
    }

    checkSafeWatchdog();
    updateTemperatureNonBlocking();

    unsigned long now = millis();
    if (now - lastStatusReport >= STATUS_REPORT_INTERVAL) {
        lastStatusReport = now;
        Serial.print("TEMP:");
        Serial.println(readTemperature(), 1);
        Serial.print("OMRON:");
        Serial.println(omronCount);
        reportStatus();
    }
}
```

---

## 4) Jetson — `uart_ws_bridge.py` (Ana Köprü / Güvenlik Beyni)

### Görevi
- Port **5006**'da ham bir WebSocket sunucusu çalıştırır (dış kütüphane kullanmadan, WS protokolünü elle uygular: el sıkışma, frame encode/decode).
- Tarayıcıdan gelen komutları ya Arduino'ya (USB) ya Raspberry Pi'ye (UART/ttyTHS1) yönlendirir.
- Arduino'dan ve Raspberry Pi'den gelen verileri (sıcaklık, fren durumu, BMS, VFD verisi, IMU) tüm bağlı tarayıcılara **broadcast** eder.
- BNO055 IMU sensöründen (I2C, `/dev/i2c-1`) anlık hız/mesafe kestirimi yapar.
- **Üç katmanlı güvenlik mimarisini** yönetir (aşağıda ayrıntılı).
- Otonom sürüş modlarını (Mod 1: sabit profil, Mod 2: hıza göre frekans) yönetir.

### Üç Katmanlı Güvenlik Mimarisi (en kritik kısım)

1. **Katman — Tarayıcı Heartbeat (yazılım, Jetson tarafı):** Sayfa Jetson'a bağlanır bağlanmaz bir Web Worker otomatik olarak `ARM` gönderir ve her `HB_MS` ms'de bir `HB` (heartbeat) yollamaya başlar. Jetson, son `HB`'den bu yana **200ms** (`HEARTBEAT_TIMEOUT`) geçtiyse `FRONT_ON`+`REAR_ON`+`MOTOR_STOP` gönderir. **Artık bir "ARM anahtarı" yok — bu koruma her zaman aktif.**
2. **Katman — Bağımsız Ping İzleme (ağ seviyesi, Jetson tarafı):** Jetson, son bağlanan tarayıcının IP'sini sürekli ICMP `ping` ile izler (JS/WS zincirinden tamamen bağımsız bir alt süreçle). **600ms** (`PING_TIMEOUT`) yanıt gelmezse aynı fren komutlarını tetikler. Bu, tarayıcının kendisi "bağlıyım" sansa bile PC'nin fiziksel olarak ağdan düştüğünü yakalar.
3. **Katman — Arduino'nun Kendi Watchdog'u (donanım seviyesi, tamamen bağımsız):** Yukarıdaki iki katman da Jetson üzerinde çalışır; Jetson'un kendisi çökerse ya da Arduino ile arasındaki USB hattı koparsa devre dışı kalırlar. Bu yüzden Jetson, Arduino'ya bağlandığı an otomatik `SAFE_ARM` gönderir ve tarayıcıdan taze heartbeat geldiği sürece her 100ms'de bir Arduino'ya `SAFE_HB` iletir. Arduino, 400ms boyunca `SAFE_HB` almazsa **kendi kararıyla** frenleri kilitler — Jetson'dan hiçbir komut beklemeden.

Sonuç: PC-Jetson kablosu koparsa (Katman 1+2), Jetson-Arduino hattı koparsa veya Jetson'un kendisi çökerse (Katman 3), sistem **varsayılan olarak fren kilitli** duruma geçer. Sadece canlı ve taze bir tarayıcı bağlantısı varken frenler serbest kalabilir.

### Diğer Önemli Noktalar
- `ARDUINO_PORT`, `ttyUSB0` yerine **sabit** `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` yolunu kullanır — USB adaptör güç dalgalanmasıyla `ttyUSB0`/`ttyUSB1` arası numarası değişse bile aynı cihazı bulur.
- Arduino'ya **yazma** başarısız olursa (`Errno 5` gibi I/O hatası), süreç bilinçli olarak `os._exit(75)` ile kapanır; systemd (`Restart=always`, `RestartSec=2`) 2 saniyede temiz bir süreç açar. (Raspberry Pi/VFD hattında zaten var olan aynı kurtarma deseni.)
- Mod 1 (`AUTONOM_START:`) sabit bir frekans profili uygular (28→56→84→112→140 Hz, her kademe 2sn).
- Mod 2 (`SPEED_MODE_START:`) BNO055'ten okunan gerçek hıza göre VFD frekansını sürekli ayarlar.

### Dosya Konumu
- Kaynak: `C:\Users\keles\OneDrive\Masaüstü\Spectraloop_Kod\uart_ws_bridge.py`
- Çalıştığı yer: `/home/jetson/reflektorlu-similasyon/uart_ws_bridge.py` (Jetson, `uart-ws-bridge.service` ile)

### Tam Kaynak Kod

```python
#!/usr/bin/env python3
import asyncio
import base64
import hashlib
import json
import os
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

# Ping tabanli ikinci, bagimsiz guvenlik katmani: tarayici/WS zincirinden
# (JS, Worker, broadcast vb.) tamamen bagimsiz olarak PC'nin ag seviyesinde
# hala erisilebilir olup olmadigini dogrudan kontrol eder. Ikisinden HANGISI
# once tetiklenirse ayni watchdog_triggered bayragini kullanir.
last_client_ip = None
last_ping_ok = 0.0
PING_TIMEOUT = 0.6

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
        asyncio.ensure_future(send_to_arduino("BUZZER_OFF"))
        asyncio.ensure_future(broadcast({"type": "safety", "data": {"armed": True, "triggered": False, "reconnected": True}}))


async def safety_watchdog():
    global watchdog_triggered
    while True:
        await asyncio.sleep(0.02)
        if not watchdog_triggered:
            if time.time() - last_heartbeat > HEARTBEAT_TIMEOUT:
                watchdog_triggered = True
                print("[Safety] Heartbeat kesildi, FRONT_ON + REAR_ON + MOTOR_STOP", flush=True)
                # KRITIK: fiili guvenlik komutlari ONCE, dashboard'a
                # log/broadcast SONRA (ve beklenmeden) - olu bir WS
                # istemcisine yazmaya calisirken burada takilip fren
                # komutunu geciktirmesin.
                await send_to_arduino("FRONT_ON", log=False)
                await send_to_arduino("REAR_ON", log=False)
                await send_to_pi("MOTOR_STOP")
                await send_to_pi("MOTOR_STOP")
                await send_to_pi("MOTOR_STOP")
                asyncio.ensure_future(cancel_autonomous("watchdog", False))
                asyncio.ensure_future(log_cmd("system", "WATCHDOG TETIKLENDI (heartbeat kesildi)"))
                asyncio.ensure_future(log_cmd("arduino", "FRONT_ON"))
                asyncio.ensure_future(log_cmd("arduino", "REAR_ON"))
                asyncio.ensure_future(broadcast({"type": "safety", "data": {"armed": True, "triggered": True}}))


async def ping_watchdog():
    """WS/tarayici zincirinden bagimsiz ikinci guvenlik katmani.

    En son baglanan istemcinin IP'sini surekli ICMP ping ile izler. PC agdan
    fiilen dusarse (ethernet kablosu cekilirse) tarayicidaki JS/Worker/WS
    kodu ne yaparsa yapsin, bu bagimsiz kontrol PING_TIMEOUT suresi icinde
    yanit alamayinca ayni watchdog_triggered mekanizmasini tetikler.
    """
    global last_ping_ok, watchdog_triggered
    current_ip = None
    proc = None
    while True:
        if last_client_ip != current_ip:
            if proc is not None:
                try:
                    proc.kill()
                except Exception:
                    pass
                proc = None
            current_ip = last_client_ip
            if current_ip:
                last_ping_ok = time.time()
                try:
                    proc = await asyncio.create_subprocess_exec(
                        "ping", "-i", "0.2", current_ip,
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.DEVNULL,
                    )
                    print("[Ping] Izleniyor: %s" % current_ip, flush=True)
                except Exception as e:
                    print("[Ping] Baslatma hatasi: %s" % e, flush=True)
                    proc = None

        if proc is not None:
            try:
                line = await asyncio.wait_for(proc.stdout.readline(), timeout=0.25)
                if line and b"bytes from" in line:
                    last_ping_ok = time.time()
            except asyncio.TimeoutError:
                pass
            except Exception:
                proc = None
        else:
            await asyncio.sleep(0.2)

        if current_ip and not watchdog_triggered:
            if time.time() - last_ping_ok > PING_TIMEOUT:
                watchdog_triggered = True
                print("[Ping] %s yanit vermiyor - FRONT_ON + REAR_ON + MOTOR_STOP" % current_ip, flush=True)
                await send_to_arduino("FRONT_ON", log=False)
                await send_to_arduino("REAR_ON", log=False)
                await send_to_pi("MOTOR_STOP")
                await send_to_pi("MOTOR_STOP")
                await send_to_pi("MOTOR_STOP")
                asyncio.ensure_future(cancel_autonomous("ping_watchdog", False))
                asyncio.ensure_future(log_cmd("system", "PING WATCHDOG TETIKLENDI (%s yanit vermiyor)" % current_ip))
                asyncio.ensure_future(log_cmd("arduino", "FRONT_ON"))
                asyncio.ensure_future(log_cmd("arduino", "REAR_ON"))
                asyncio.ensure_future(broadcast({"type": "safety", "data": {"armed": True, "triggered": True}}))


# Arduino'nun kendi SAFE_HB watchdog'unu besler. Bu sinyal SADECE tarayicidan
# gelen HB tazeyken gonderilir - yani PC<->Jetson baglantisi ya da Jetson
# sureci kesilirse bu gonderim de otomatik olarak durur ve Arduino kendi
# SAFE_HB_TIMEOUT_MS suresi icinde bagimsiz olarak fren kilitler. Boylece
# Jetson<->Arduino seri hatti aninda calissa da calismasa da (USB kopmasi,
# port yeniden numaralanmasi, DMA hatasi vb.) fren garantiye alinir.
ARDUINO_SAFE_HB_INTERVAL = 0.1


async def arduino_safety_heartbeat():
    while True:
        await asyncio.sleep(ARDUINO_SAFE_HB_INTERVAL)
        if not watchdog_triggered and (time.time() - last_heartbeat) < HEARTBEAT_TIMEOUT:
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


async def handle_client(reader, writer):
    global last_client_ip
    peer = writer.get_extra_info("peername")
    ok = await ws_handshake(reader, writer)
    if not ok:
        writer.close()
        return
    print("[WS] Tarayici bagli: %s" % str(peer))
    if peer:
        last_client_ip = peer[0]
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
    asyncio.ensure_future(ping_watchdog())
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
```

---

## 5) Raspberry Pi — `vfd_modbus.py` (Motor / VFD Kontrolü)

### Görevi
Jetson'dan UART (`ttyTHS1` → Raspberry Pi'nin karşı ucu) üzerinden gelen metin komutlarını alır (`MOTOR_...`, `VFD_READ...`, `VFD_WRITE...`), bunları **Modbus RTU** çerçevelerine çevirip **RS485** üzerinden (`/dev/ttyAMA3`, 9600 baud) Delta MS300 marka frekans invertörüne (VFD) gönderir. VFD'nin cevabını okuyup JSON olarak Jetson'a geri yollar (Jetson bunu `serial_reader()` ile okuyup tüm tarayıcılara broadcast eder).

### Komut Protokolü (Jetson → Raspberry Pi)
| Komut | Etki |
|---|---|
| `MOTOR_DIR_FORWARD` / `MOTOR_DIR_REVERSE` | Motor yönünü ayarla (Modbus register `0x2000`) |
| `MOTOR_FREQ:<hz>` | Hedef frekansı yaz (register `0x2001`, x100 ölçekli) |
| `MOTOR_GO` | Motoru mevcut yönde çalıştır |
| `MOTOR_STOP` | Motoru durdur |
| `VFD_READ_ALL` | Tanımlı tüm VFD parametrelerini oku |
| `VFD_READ:<kod>` | Tek bir parametreyi oku (örn. `01-12` hızlanma süresi) |
| `VFD_WRITE:<kod>:<değer>` | Tek bir parametreye yaz |

### Dosya Konumu
- Kaynak: `C:\Users\keles\OneDrive\Masaüstü\Spectraloop_Kod\vfd_modbus.py`
- Çalıştığı yer: Raspberry Pi üzerinde (dosyanın kendi docstring'i "Pi tarafi" diyor). Bu makineye SSH erişimim olmadığı için tam dosya yolunu/servis adını doğrulayamadım — muhtemelen benzer bir systemd servisiyle otomatik başlatılıyor.

### Tam Kaynak Kod

```python
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
```

---

## 6) Web Arayüzü — `index.html` (Dashboard)

Bu dosya `bundle.js` gibi ayrı bir dosya **yüklemez** — tüm JavaScript sayfanın içine gömülü (tek parça, minify edilmiş). 61KB civarında olduğu için buraya ham hâliyle eklemedim; ihtiyaç olursa dosyayı doğrudan aç:
`C:\Users\keles\OneDrive\Masaüstü\Spectraloop_Kod\index.html`

### Fonksiyonel Özet
- Sayfa açılır açılmaz bir **Web Worker** içinde WebSocket bağlantısı kurar (`ws://192.168.9.101:5006`).
- Worker bağlanır bağlanmaz otomatik `ARM` gönderir, ardından periyodik `HB` (heartbeat) göndermeye başlar. Bağlantı koparsa (`onclose`/`onerror`) heartbeat interval'i durur — bu, Jetson tarafındaki Katman-1 watchdog'un tetiklenme sebebidir.
- "Hepsi" butonu: `FRONT_ON` ve `REAR_ON` komutlarını art arda gönderir (Jetson'daki otomatik watchdog'un gönderdiği komutlarla **birebir aynı**).
- Acil stop, kontaktör, SSR, stop lambası, motor yön/frekans, otonom Mod 1/Mod 2 başlat-durdur butonları hepsi aynı WebSocket üzerinden düz metin komutlar gönderir.
- `buzzer-btn` sadece **durum göstergesidir** — Arduino'dan gelen `STATUS` JSON'undaki `buzzer` alanına göre CSS sınıfı değişir; buzer'ı doğrudan kontrol eden bir buton **yoktur** (yukarıda Arduino bölümünde açıklandığı gibi, buzzer donanımda tamamen otomatik).
- Diğer sayfalar (aynı klasörde): `vfd.html` (VFD parametreleri), `bms.html` (batarya izleme), `pintest.html` (röle/pin testi) — hepsi aynı WebSocket sunucusuna (port 5006) bağlanır.

---

## 7) Ağ / Sistem Ayarları

- Jetson'un statik IP'si: **192.168.9.101/24**, gateway **192.168.9.1**, arayüz `eth0`.
- Bu ayar, açılışta çalışan `kapsul-eth0.service` (systemd, `ip addr add` + `ip route add` komutlarını çalıştırır) ile kalıcı hale getirilmiş. Ayrıntılı kurulum adımları: `C:\Users\keles\OneDrive\Masaüstü\jetsonagayarları.txt`.
- Jetson `NetworkManager` tarafından **yönetilmiyor** (`eth0` unmanaged) — bu yüzden `nmcli` ile değil doğrudan `ip` komutlarıyla ayarlanıyor.
- Dashboard: `http://192.168.9.101:3000/`
- WebSocket: `ws://192.168.9.101:5006/`
- Jetson SSH: kullanıcı `jetson`, IP `192.168.9.101` (parola bilgisi bu dokümana yazılmadı — güvenlik nedeniyle).

---

## 8) Bugünkü Oturumda Bulunan ve Düzeltilen Sorunlar (özet)

1. **"ARM" güvenlik anahtarı mantığı tamamen kaldırıldı** — koruma artık koşulsuz her zaman aktif (kod: `uart_ws_bridge.py`, ilgili tüm `safety_armed` kontrolleri silindi).
2. **Hız Modu'nu (Mod 2) bloke eden ölü frontend kodu kaldırıldı** — artık var olmayan bir `safety-switch` DOM elemanını arayıp hep "önce güvenlik anahtarını aç" diyordu.
3. **Kritik: Arduino USB portu sabit `/dev/ttyUSB0`'a bağlıydı**, ama cihaz güç dalgalanmalarıyla `ttyUSB1`'e kayabiliyordu; üstüne üstlük yazma hatası oluşunca kod **hiçbir zaman** yeniden bağlanmıyordu (bağlantı sonsuza dek "ölü" kalıyordu). Artık sabit `/dev/serial/by-id/...` yolu kullanılıyor ve yazma hatasında süreç 2 saniyede temiz şekilde kendini yeniden başlatıyor.
4. **⚠️ Donanım sorunu (yazılımla tam çözülemez):** Jetson'un `dmesg` kaydında açılıştan beri onlarca kez "OC ALARM" (aşırı akım alarmı) var ve Arduino'nun USB-seri adaptörü sık sık takılıp çıkıyor. Bu gerçek bir elektriksel sorun — USB kablosu/portu değiştirilmeli, röle kartının Arduino'nun USB 5V hattından değil ayrı bir güç kaynağından beslenip beslenmediği kontrol edilmeli.
