# Pengingat Jadwal Sholat — ESP32 + RTC DS3231 + DFPlayer Mini

Alat kecil yang mengingatkanmu setiap waktu sholat tiba. Jam RTC DS3231
membaca waktu; begitu jadwal sholat tiba, suara adzan berkumandang dari
speaker, nama sholat muncul di LCD, dan kamu tinggal tekan tombol untuk
menghentikan adzan.

Jadwal sholat diambil otomatis dari API [equran.id](https://equran.id)
(`POST /api/v2/shalat`) setiap hari pukul 00:05 bila WiFi tersambung —
jadi jadwalnya selalu mengikuti tanggal dan kotamu. Kalau WiFi/API gagal,
dipakai jadwal bawaan (Kota Bandung) yang ada di sketch.

Video penjelasan (60 detik):
https://youtube.com/shorts/ — (link menyusul setelah upload)

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat |
| RTC DS3231 | Jam akurat + baterai CR2032 |
| DFPlayer Mini | Pemutar MP3 (kartu microSD) |
| Speaker 3W 4Ω | Suara adzan |
| LCD 16x2 I2C (0x27) | Tampil jam + sholat berikutnya / nama sholat |
| LED hijau | Menyala saat adzan berbunyi |
| Tombol | Hentikan adzan (INPUT_PULLUP) |

## Wiring

| Dari | Ke |
|---|---|
| RTC SDA | ESP32 GPIO21 |
| RTC SCL | ESP32 GPIO22 |
| RTC VCC | ESP32 3V3 |
| LCD SDA / SCL | Bus I2C yang sama (GPIO21 / GPIO22) |
| LCD VCC | ESP32 VIN 5V |
| DFPlayer RX | ESP32 GPIO17 (TX2) |
| DFPlayer TX | ESP32 GPIO16 (RX2) |
| DFPlayer VCC | ESP32 VIN 5V |
| DFPlayer SPK1 / SPK2 | Speaker 3W 4Ω |
| LED hijau | ESP32 GPIO32 (seri 220 Ω ke GND) |
| Tombol | ESP32 GPIO14 → GND |
| Semua GND | Digabung |

Skema lengkap: [schematic.png](schematic.png)

## Kartu microSD DFPlayer (FAT32)

Buat folder `mp3`, isi file adzan bernama `0001.mp3`. File MP3 adzan
bebas — rekam sendiri atau unduh yang lisensinya bebas (bukan dari
rekaman komersial berhak cipta).

## Cara kerja

1. ESP32 mengecek waktu dari RTC tiap detik.
2. Jadwal cocok (mis. Dzuhur 11:39) → DFPlayer memutar `0001.mp3`,
   LCD tampil "WAKTUNYA SHOLAT!" + nama sholat, LED hijau menyala,
   notifikasi Telegram terkirim (bila `USE_TELEGRAM` 1).
3. Tekan tombol → adzan berhenti, LCD tampil "Selamat sholat!".
4. Adzan berhenti sendiri bila track selesai atau setelah 5 menit.
5. LCD idle menampilkan jam + sholat berikutnya ("Nxt Dzuhur 11:39").
6. Tiap 00:05, jadwal hari ini diambil ulang dari equran.id.

## Konfigurasi (di awal sketch)

- `USE_JADWAL_API` (1) — ambil jadwal dari equran.id bila WiFi up.
  Set 0 untuk memakai jadwal bawaan saja tanpa WiFi.
- `USE_TELEGRAM` (1) — kirim notifikasi Telegram. Set 0 untuk tanpa Telegram.
- `WIFI_SSID` / `WIFI_PASS` — WiFi rumahmu.
- `BOT_TOKEN` / `CHAT_ID` — token bot dan chat ID Telegram.
- `PROVINSI` / `KABKOTA` — wilayah untuk jadwal sholat (default Jawa Barat / Kota Bandung).
- `jadwal[]` — jadwal bawaan 5 waktu (Subuh, Dzuhur, Ashar, Maghrib, Isya).
- `VOLUME_ADZAN` (25) — volume 0–30.

## Library yang dibutuhkan

| Library | Versi | Keterangan |
|---|---|---|
| RTClib | 2.1.4 | RTC DS3231 |
| LiquidCrystal_I2C | 1.1.2 | LCD 16x2 I2C |
| DFRobotDFPlayerMini | 1.0.6 | DFPlayer Mini |
| ArduinoJson | 6.21.5 | Parsing jadwal dari equran.id (WAJIB v6 — v7 tidak dipakai di sini) |
| UniversalTelegramBot | 1.3.0 | Notifikasi Telegram |

Semua library di atas juga tersedia sebagai ZIP di folder [`libraries/`](libraries/)
— install lewat Arduino IDE: Sketch → Include Library → Add .ZIP Library.

## Compile

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 PengingatSholat
```

Terverifikasi 20 Nov 2026: sketch 1.132.129 byte (86% flash),
RAM 53.488 byte (16%), ESP32 core 4.0.0.

## Lisensi

MIT — lihat [LICENSE](LICENSE).
