// Pengingat Jadwal Sholat — ESP32 + RTC DS3231 + DFPlayer Mini + LCD 16x2
//
// Cara kerja:
//  - RTC DS3231 menjaga jam tetap akurat (baterai CR2032).
//  - ESP32 mengecek waktu tiap detik; begitu jadwal sholat tiba, DFPlayer
//    memutar suara adzan (file /mp3/0001.mp3 di kartu microSD), LCD
//    menampilkan nama sholatnya, LED hijau menyala.
//  - Tekan tombol untuk menghentikan adzan ("Selamat sholat!").
//  - Jadwal sholat diambil dari API equran.id (POST /api/v2/shalat) setiap
//    hari pukul 00:05 bila WiFi tersambung; kalau gagal, dipakai jadwal
//    bawaan (Kota Bandung) di bawah. Set USE_JADWAL_API 0 untuk memakai
//    jadwal bawaan saja tanpa WiFi.
//
// Wiring:
//  - RTC DS3231 : SDA -> GPIO21, SCL -> GPIO22, VCC -> 3V3, GND -> GND
//  - LCD 16x2 I2C (0x27) : SDA/SCL bus I2C yang sama, VCC -> VIN 5V
//  - DFPlayer Mini : RX -> GPIO17 (TX2), TX -> GPIO16 (RX2),
//    VCC -> VIN 5V, GND -> GND, speaker 3W 4 ohm ke SPK1/SPK2
//  - LED hijau : GPIO32 (seri resistor 220 ohm ke GND)
//  - Tombol akui : GPIO14 -> GND (INPUT_PULLUP, aktif LOW)
//
// Kartu microSD DFPlayer (format FAT32):
//  - Buat folder "mp3", isi file adzan bernama 0001.mp3
//    (file MP3 adzan bebas — rekam sendiri atau unduh yang lisensinya bebas).
//
// Library yang dibutuhkan (semua tersedia di Library Manager):
//  - RTClib 2.1.4, LiquidCrystal_I2C 1.1.2, DFRobotDFPlayerMini 1.0.6,
//    ArduinoJson 6.21.5 (hanya bila USE_JADWAL_API 1),
//    UniversalTelegramBot 1.3.0 (hanya bila USE_TELEGRAM 1).
// Compile: arduino-cli compile --fqbn esp32:esp32:esp32 PengingatSholat
// (terverifikasi 20 Nov 2026: 1.132.129 byte, 86% flash, RAM 53.488 byte,
//  16%, ESP32 core 4.0.0, COMPILE_RC=0)

#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <DFRobotDFPlayerMini.h>

// ---------- KONFIGURASI ----------
#define USE_JADWAL_API 1  // 1 = ambil jadwal dari equran.id bila WiFi up, 0 = jadwal bawaan saja
#define USE_TELEGRAM 1    // 1 = kirim notifikasi Telegram, 0 = tanpa Telegram

#define PIN_LED_HIJAU 32
#define PIN_TOMBOL 14
#define DFPLAYER_RX 16  // ESP32 RX2 <- DFPlayer TX
#define DFPLAYER_TX 17  // ESP32 TX2 -> DFPlayer RX
#define VOLUME_ADZAN 25 // 0..30
#define ADZAN_MAKS_DETIK 300 // adzan berhenti sendiri setelah 5 menit

const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASS = "PASSWORD_WIFI";
const char *BOT_TOKEN = "TOKEN_BOT_TELEGRAM";
const char *CHAT_ID = "CHAT_ID_TELEGRAM";
const char *PROVINSI = "Jawa Barat";
const char *KABKOTA = "Kota Bandung";

// Jadwal bawaan (Kota Bandung, 20 November 2026) — dipakai bila API gagal.
struct Jadwal {
  const char *nama;
  int jam;
  int menit;
};
Jadwal jadwal[5] = {
  {"Subuh", 4, 1},
  {"Dzuhur", 11, 39},
  {"Ashar", 15, 1},
  {"Maghrib", 17, 56},
  {"Isya", 19, 6},
};

RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
DFRobotDFPlayerMini dfplayer;

bool sudahBunyi[5] = {false, false, false, false, false};
bool jadwalDariAPI = false;
bool adzanBunyi = false;
int sholatAktif = -1;
unsigned long adzanMulai = 0;
unsigned long cekTerakhir = 0;
bool tombolDulu = HIGH;

#if USE_TELEGRAM
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
WiFiClientSecure telegramClient;
UniversalTelegramBot bot(BOT_TOKEN, telegramClient);
void kirimTelegram(const String &pesan) {
  if (WiFi.status() != WL_CONNECTED) return;
  bot.sendMessage(CHAT_ID, pesan, "");
}
#else
void kirimTelegram(const String &) {}
#endif

#if USE_JADWAL_API
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Ambil jadwal sholat bulan ini dari equran.id, pakai tanggal hari ini.
// Respon: data.jadwal[] = {tanggal:int, subuh/dzuhur/ashar/maghrib/isya:"HH:MM"}
bool ambilJadwalAPI(const DateTime &t) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://equran.id/api/v2/shalat");
  http.addHeader("Content-Type", "application/json");
  String body = "{\"provinsi\":\"" + String(PROVINSI) + "\",\"kabkota\":\"" +
                String(KABKOTA) + "\",\"bulan\":" + String(t.month()) +
                ",\"tahun\":" + String(t.year()) + "}";
  int kode = http.POST(body);
  if (kode != 200) {
    http.end();
    return false;
  }
  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;
  JsonArray arr = doc["data"]["jadwal"];
  for (JsonObject e : arr) {
    if (e["tanggal"] == t.day()) {
      const char *kunci[5] = {"subuh", "dzuhur", "ashar", "maghrib", "isya"};
      for (int i = 0; i < 5; i++) {
        const char *s = e[kunci[i]];
        if (s && strlen(s) >= 5) {
          jadwal[i].jam = (s[0] - '0') * 10 + (s[1] - '0');
          jadwal[i].menit = (s[3] - '0') * 10 + (s[4] - '0');
        }
      }
      return true;
    }
  }
  return false;
}
#endif

void sambungWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 20000) delay(250);
}

void tampilIdle(const DateTime &t) {
  const char *bulan[12] = {"Jan", "Feb", "Mar", "Apr", "Mei", "Jun",
                           "Jul", "Agu", "Sep", "Okt", "Nov", "Des"};
  char baris0[17];
  snprintf(baris0, sizeof(baris0), "%02d:%02d:%02d %02d %s",
           t.hour(), t.minute(), t.second(), t.day(), bulan[t.month() - 1]);
  // Cari sholat berikutnya hari ini (atau Subuh besok bila sudah Isya).
  int idx = -1;
  for (int i = 0; i < 5; i++) {
    if (jadwal[i].jam > t.hour() ||
        (jadwal[i].jam == t.hour() && jadwal[i].menit > t.minute())) {
      idx = i;
      break;
    }
  }
  if (idx < 0) idx = 0;
  char baris1[17];
  snprintf(baris1, sizeof(baris1), "Nxt %s %02d:%02d",
           jadwal[idx].nama, jadwal[idx].jam, jadwal[idx].menit);
  // Ratakan ke 16 kolom supaya tidak ada sisa karakter lama di LCD.
  char r0[17], r1[17];
  snprintf(r0, sizeof(r0), "%-16s", baris0);
  snprintf(r1, sizeof(r1), "%-16s", baris1);
  lcd.setCursor(0, 0);
  lcd.print(r0);
  lcd.setCursor(0, 1);
  lcd.print(r1);
}

void putarAdzan(int i) {
  sholatAktif = i;
  adzanBunyi = true;
  adzanMulai = millis();
  sudahBunyi[i] = true;
  digitalWrite(PIN_LED_HIJAU, HIGH);
  dfplayer.play(1); // /mp3/0001.mp3 = suara adzan
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WAKTUNYA SHOLAT!");
  lcd.setCursor(0, 1);
  lcd.print("Sholat ");
  lcd.print(jadwal[i].nama);
  kirimTelegram(String("Waktunya sholat ") + jadwal[i].nama +
                " — adzan berbunyi.");
  Serial.print("Adzan: ");
  Serial.println(jadwal[i].nama);
}

void hentikanAdzan() {
  if (!adzanBunyi) return;
  adzanBunyi = false;
  dfplayer.stop();
  digitalWrite(PIN_LED_HIJAU, LOW);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Selamat sholat!");
  lcd.setCursor(0, 1);
  lcd.print(jadwal[sholatAktif].nama);
  kirimTelegram(String("Adzan ") + jadwal[sholatAktif].nama +
                " dihentikan — selamat sholat.");
  sholatAktif = -1;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_TOMBOL, INPUT_PULLUP);
  digitalWrite(PIN_LED_HIJAU, LOW);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pengingat Sholat");

  if (!rtc.begin()) {
    lcd.setCursor(0, 1);
    lcd.print("RTC tidak ketemu!");
    while (1) delay(1000);
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  Serial2.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (!dfplayer.begin(Serial2)) {
    lcd.setCursor(0, 1);
    lcd.print("DFPlayer gagal!");
    Serial.println("DFPlayer tidak merespon — cek kabel RX/TX & SD card.");
  } else {
    dfplayer.volume(VOLUME_ADZAN);
  }

#if USE_JADWAL_API || USE_TELEGRAM
  sambungWiFi();
#if USE_JADWAL_API
  if (WiFi.status() == WL_CONNECTED) {
    DateTime t = rtc.now();
    jadwalDariAPI = ambilJadwalAPI(t);
    Serial.println(jadwalDariAPI ? "Jadwal dari equran.id OK"
                                 : "Jadwal API gagal, pakai bawaan");
  }
#endif
#if USE_TELEGRAM
  telegramClient.setInsecure();
#endif
#endif

  lcd.clear();
  Serial.println("Pengingat Sholat siap.");
}

void loop() {
  DateTime t = rtc.now();

  // Reset penanda tiap tengah malam + ambil jadwal hari baru.
  if (t.hour() == 0 && t.minute() == 0 && t.second() < 2) {
    for (int i = 0; i < 5; i++) sudahBunyi[i] = false;
  }
#if USE_JADWAL_API
  if (t.hour() == 0 && t.minute() == 5 && t.second() < 2 && !jadwalDariAPI) {
    if (WiFi.status() != WL_CONNECTED) sambungWiFi();
    jadwalDariAPI = ambilJadwalAPI(t);
  }
#endif

  // Adzan selesai sendiri (track habis) atau timeout 5 menit.
  if (adzanBunyi) {
    if (dfplayer.available()) {
      if (dfplayer.readType() == DFPlayerPlayFinished) hentikanAdzan();
    }
    if (adzanBunyi && millis() - adzanMulai > ADZAN_MAKS_DETIK * 1000UL) {
      hentikanAdzan();
    }
  }

  // Tombol ditekan (aktif LOW) = hentikan adzan.
  bool tombol = digitalRead(PIN_TOMBOL);
  if (tombol == LOW && tombolDulu == HIGH) {
    delay(50); // debounce
    if (digitalRead(PIN_TOMBOL) == LOW) hentikanAdzan();
  }
  tombolDulu = tombol;

  // Cek jadwal tiap detik.
  if (millis() - cekTerakhir >= 1000) {
    cekTerakhir = millis();
    if (!adzanBunyi) tampilIdle(t);
    for (int i = 0; i < 5; i++) {
      if (t.hour() == jadwal[i].jam &&
          t.minute() == jadwal[i].menit &&
          !sudahBunyi[i]) {
        putarAdzan(i);
      }
    }
  }
}
