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
 *   AYNI FONKSIYONDA: HER IKI FREN DE aktif oldugunda kontaktor de OTOMATIK
 *   kesilir (VFD'ye/surucuye guc gitmez) - sebep fark etmez (manuel buton,
 *   ACIL STOP veya watchdog tetiklemesi). Tek yonlu: frenler acilinca
 *   kontaktor kendiliginden geri devreye girmez, CONTACTOR_ON ile elle
 *   acilmasi gerekir.
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
 *   OMRON_RESET -> omron sayacini sifirla
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
 * Role mantigi (on/arka fren, buzzer, stop lambasi - fiziksel testle
 * dogrulanmis, bu kart AKTIF-HIGH):
 *   HIGH = role kapali = yuk ON
 *   LOW  = role acik   = yuk OFF
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

// NOT: Fiziksel test sonucu role karti bu dort cikis (on/arka fren, buzzer,
// stop lambasi) icin beklenenin TERSI yonde bagli oldugu dogrulandi - kod
// "ON" gonderdiginde role fiilen acik/serbest kaliyor, "OFF" gonderdiginde
// fiilen devreye giriyordu. RELAY_ON/RELAY_OFF burada yer degistirilerek
// yazilimdaki brakeFront/brakeRear/buzzerOn/stopLightOn anlamlari (true =
// gercekten devrede) ile fiziksel role cikisi birbirine uydurulmustur.
const int RELAY_ON  = HIGH;
const int RELAY_OFF = LOW;

// 2026-08-12: On/arka fren valflerinin pnomatik borulari fiziksel olarak
// yer degistirildi (kullanici tarafindan) - bu iki cikis icin ON/OFF anlami
// tersine dondu. SADECE fren pinlerine ozel, buzzer/stop lambasi/kontaktor
// yukarsidaki RELAY_ON/RELAY_OFF'u aynen kullanmaya devam ediyor.
const int BRAKE_ON  = RELAY_OFF;
const int BRAKE_OFF = RELAY_ON;

// ── Durum degiskenleri ────────────────────────────────────────────────────────
bool brakeFront  = false;
bool brakeRear   = false;
bool buzzerOn    = false;
bool flasherOn   = false;
bool stopLightOn = false;
bool contactorOn = false;
bool ssrOn       = false;

// ── Manuel buzzer (dashboard "Buzzer"/"Bip" butonlari) ──────────────────────
// NOT: buzzerOn, updateAlarmFromBrakes() tarafindan da yonetiliyor (iki fren
// de aktifken otomatik ON). Manuel BUZZER_ON sonrasi baska bir fren komutu
// gelirse (bothOn hala false ise) otomatik mantik buzzerOn'u tekrar false'a
// cekip kapatabilir - bu kasitli, guvenlik-kritik olmadigi icin ayri bir
// "override" mekanizmasi eklenmedi.
bool buzzerBeeping = false;
unsigned long buzzerBeepStart = 0;
const unsigned long BUZZER_BEEP_MS = 300UL;

// ── Guvenlik watchdog (Jetson<->Arduino hatti) ──────────────────────────────
// Jetson, guvenlik switchi acikken periyodik SAFE_HB gonderir. Bu sinyal
// SAFE_HB_TIMEOUT_MS suresince kesilirse (Jetson coktu, USB/seri hatti
// koptu/tikandi, ya da Jetson'in PC ile baglantisi kesildi) Arduino
// KENDI KARARIYLA frenleri kilitler - Jetson'dan ayrica FRONT_ON/REAR_ON
// komutu gelmesini beklemez.
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
// DS18B20'nin donusum suresi (~750ms 12-bit) ana donguyu (fren komutlarini)
// kilitlemesin diye setWaitForConversion(false) ile asenkron okunuyor:
// her donguda once donusum baslatilir, ~750ms sonra sonuc okunur, bu sirada
// loop() diger islerine (seri komut, fren vb.) devam eder.
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
// Kural: HER IKI FREN DE aktifse buzzer ON.
//        HERHANGI BIR FREN aktifse stop lambasi ON (gercek arac stop lambasi
//        mantigi - tek fren de olsa yanar).
// Flasor kalici olarak devre disi (baska hicbir yerde bu pine dokunulmuyor).
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

    // Iki fren de aktif oldugunda kontaktoru kes - VFD'ye (surucuye) guc
    // gitmesin. Sebep onemli degil: manuel buton, ACIL STOP veya watchdog
    // (checkSafeWatchdog) - hepsi brakeFront/brakeRear'i set edip bu
    // fonksiyonu cagiriyor, tek kaynaktan yonetiliyor.
    // IKI YONLU: frenlerin ikisi de acilinca (bothOn'dan false'a donunce)
    // kontaktor OTOMATIK geri devreye girer - kullanicinin elle tekrar
    // CONTACTOR_ON gondermesine gerek yok.
    if (bothOn && contactorOn) {
        contactorOn = false;
        digitalWrite(PIN_CONTACTOR, LOW);
        Serial.println("EVENT:CONTACTOR_AUTO_CUT_BOTH_BRAKES");
    } else if (!bothOn && !contactorOn) {
        contactorOn = true;
        digitalWrite(PIN_CONTACTOR, HIGH);
        Serial.println("EVENT:CONTACTOR_AUTO_RESTORE_BRAKES_RELEASED");
    }
}

// Jetson'dan SAFE_HB sinyali kesilirse (guvenlik switchi acikken) frenleri
// kendi basina kilitler. Bir kere tetiklenince FRONT_OFF/REAR_OFF gelene
// kadar acik kalmaz - kullanici elle serbest birakmali (diger tum acil
// durum senaryolariyla ayni mantik).
void checkSafeWatchdog() {
    if (safeArmed && !safeTripped) {
        if (millis() - lastSafeHb > SAFE_HB_TIMEOUT_MS) {
            safeTripped = true;
            digitalWrite(PIN_RELAY_FRONT, BRAKE_ON);
            digitalWrite(PIN_RELAY_REAR, BRAKE_ON);
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
        digitalWrite(PIN_RELAY_FRONT, BRAKE_ON);
        brakeFront = true;
        updateAlarmFromBrakes();
        Serial.print("OK:"); Serial.println(line);
        return;
    }
    if (line == "FRONT_OFF" || line == "FRONT_ABS_OFF") {
        digitalWrite(PIN_RELAY_FRONT, BRAKE_OFF);
        brakeFront = false;
        updateAlarmFromBrakes();
        Serial.print("OK:"); Serial.println(line);
        return;
    }
    if (line == "REAR_ON" || line == "REAR_ABS_ON") {
        digitalWrite(PIN_RELAY_REAR, BRAKE_ON);
        brakeRear = true;
        updateAlarmFromBrakes();
        Serial.print("OK:"); Serial.println(line);
        return;
    }
    if (line == "REAR_OFF" || line == "REAR_ABS_OFF") {
        digitalWrite(PIN_RELAY_REAR, BRAKE_OFF);
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

    if (line == "BUZZER_ON") {
        buzzerOn = true;
        buzzerBeeping = false;
        digitalWrite(PIN_BUZZER, RELAY_ON);
        Serial.println("OK:BUZZER_ON");
        return;
    }
    if (line == "BUZZER_OFF") {
        buzzerOn = false;
        buzzerBeeping = false;
        digitalWrite(PIN_BUZZER, RELAY_OFF);
        Serial.println("OK:BUZZER_OFF");
        return;
    }
    if (line == "BUZZER_BEEP") {
        digitalWrite(PIN_BUZZER, RELAY_ON);
        buzzerBeeping = true;
        buzzerBeepStart = millis();
        Serial.println("OK:BUZZER_BEEP");
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
    if (line == "OMRON_RESET") {
        // omronCount ISR icinde de degistiriliyor; yazma sirasinda ISR
        // araya girip yarim/bozuk deger olusmasin diye kesmeleri kisaca kapat.
        noInterrupts();
        omronCount = 0;
        interrupts();
        Serial.println("OK:OMRON_RESET");
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

    int outputs[] = {PIN_BUZZER, PIN_STOP_LIGHT};
    for (int i = 0; i < 2; i++) {
        pinMode(outputs[i], OUTPUT);
        digitalWrite(outputs[i], RELAY_OFF);
    }

    // Fren pinleri: BRAKE_ON/BRAKE_OFF kullaniyor (RELAY_ON/OFF'un tersi,
    // bkz. yukaridaki BRAKE_ON/BRAKE_OFF tanimi) - baslangicta guvenli
    // (fren SERBEST) durum icin BRAKE_OFF yaziliyor.
    pinMode(PIN_RELAY_FRONT, OUTPUT);
    digitalWrite(PIN_RELAY_FRONT, BRAKE_OFF);
    pinMode(PIN_RELAY_REAR, OUTPUT);
    digitalWrite(PIN_RELAY_REAR, BRAKE_OFF);

    // FLASOR, SSR ve KONTAKTOR: RELAY_ON/RELAY_OFF sabitlerini kullanmiyor,
    // dogrudan ham HIGH/LOW ile kontrol ediliyor - baslangic KAPALI durumu
    // icin LOW yaziliyor.
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

    // Flasor blink dongusu KALDIRILDI - flasor kalici olarak devre disi.

    checkSafeWatchdog();
    updateTemperatureNonBlocking();

    unsigned long now = millis();

    if (buzzerBeeping && (now - buzzerBeepStart >= BUZZER_BEEP_MS)) {
        buzzerBeeping = false;
        // Bip bitince buzzer'i GERCEK durumuna (buzzerOn) geri dondur --
        // ornegin bip sirasinda iki fren de aktif olup otomatik ON tetiklendiyse
        // ON kalmali, aksi halde OFF.
        digitalWrite(PIN_BUZZER, buzzerOn ? RELAY_ON : RELAY_OFF);
    }

    if (now - lastStatusReport >= STATUS_REPORT_INTERVAL) {
        lastStatusReport = now;
        Serial.print("TEMP:");
        Serial.println(readTemperature(), 1);
        Serial.print("OMRON:");
        Serial.println(omronCount);
        reportStatus();
    }
}
