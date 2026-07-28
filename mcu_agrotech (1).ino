/*
  ===========================================================
  MCU AGROTECH — Panel Monitoring & Kontrol Relay
  ===========================================================
  Alur besar program ini:
  1. ESP32 baca sensor (suhu, kelembapan udara, kelembapan tanah)
  2. Data itu ditampilkan di LCD & dikirim ke ThingSpeak
  3. ESP32 juga jadi WEB SERVER — dia nge-host file dashboard
     (index.html, style.css, script.js) langsung dari LittleFS,
     jadi siapa pun di WiFi yang sama bisa buka dashboard-nya
     lewat IP ESP32, tanpa perlu laptop kita nyala/localhost.
  4. Dashboard di browser baca data dari ThingSpeak (bukan
     langsung dari ESP32), dan tombol relay di dashboard nulis
     status ke ThingSpeak field4/field5.
  5. ESP32 secara berkala "nengok" ke ThingSpeak, baca field4/5,
     lalu nyalain/matiin relay fisik sesuai itu.
  ===========================================================
*/

#include <WiFi.h>          // buat konek ke jaringan WiFi
#include <WebServer.h>     // biar ESP32 bisa jadi web server (port 80)
#include <ThingSpeak.h>    // library resmi buat kirim/baca data ThingSpeak
#include <DHT.h>           // buat sensor suhu & kelembapan DHT22
#include <LiquidCrystal_I2C.h> // buat LCD 20x4 lewat jalur I2C
#include <LittleFS.h>      // filesystem di flash ESP32, tempat nyimpen file web

// ===================== KONFIGURASI WIFI =====================
const char* ssid = "UGMURO-INET";
const char* password = "Gepuk15000";
// ESP32 konek sebagai STATION (client) ke router ini, bukan bikin
// access point sendiri — makanya dashboard-nya bisa diakses device
// lain yang nyambung ke router yang sama.

// ===================== KONFIGURASI THINGSPEAK =====================
const char* writeAPIKey = "ALZ1HERJNTLNREOD"; // buat NULIS data (field1-3)
const char* readAPIKey  = "91HT99SJN6YRO2MQ"; // buat BACA data (field4-5, status relay)
const unsigned long channelID = 3403067;      // ID channel ThingSpeak (satu channel aja)
WiFiClient client;                            // koneksi TCP yang dipakai library ThingSpeak

// ===================== WEB SERVER =====================
WebServer server(80);
// server ini yang bakal ngasih file index.html/style.css/script.js
// ke browser siapa pun yang buka http://IP-ESP32-lo

// ===================== SENSOR DHT22 =====================
#define DHTPIN 25
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ===================== LCD I2C =====================
// alamat 0x27 itu alamat I2C paling umum buat modul LCD 20x4,
// kalau LCD lo gak nyala/nge-blank, coba ganti ke 0x3F
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ===================== PIN-PIN =====================
#define SOIL_PIN 34   // input analog, baca sensor kelembapan tanah
#define RELAY1 14     // relay 1 (dikontrol dari dashboard)
#define RELAY2 17     // relay 2 (dikontrol dari dashboard)
#define RELAY3 26     // relay 3 (otomatis, ikut kelembapan tanah)

// ===================== VARIABEL DATA SENSOR =====================
// variabel global ini yang "menjembatani" antara bagian pembacaan
// sensor, tampilan LCD, dan pengiriman ke ThingSpeak — semua bagian
// program baca/tulis dari sini
float t = 0;      // suhu terakhir yang berhasil dibaca
float h = 0;      // kelembapan udara terakhir
int soil = 0;     // kelembapan tanah terakhir (0-100%)

// ===================== STATUS RELAY (dari dashboard) =====================
// nilai ini DIISI oleh hasil baca ThingSpeak (bukan oleh sensor),
// jadi ini yang bikin tombol di web bisa "menyuruh" relay fisik
int relay1State = 0;
int relay2State = 0;

// ===================== TIMER NON-BLOCKING =====================
// pakai pola "millis() - lastX > interval" biar ESP32 gak nge-freeze
// nunggu delay() panjang — server.handleClient() tetap jalan tiap loop
unsigned long lastSend = 0;       // kapan terakhir kirim data ke ThingSpeak
unsigned long lastRead = 0;       // kapan terakhir baca sensor
unsigned long lastRelayPoll = 0;  // kapan terakhir cek status relay dari ThingSpeak


void setup() {
  Serial.begin(115200); // buat debugging lewat Serial Monitor

  // ---------- INIT LCD ----------
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Starting...");
  delay(1500);
  lcd.clear();

  // ---------- MOUNT LITTLEFS ----------
  // "mount" = nyalain akses ke partisi flash yang isinya file web.
  // parameter (true) artinya: kalau LittleFS belum pernah diformat,
  // format otomatis dulu (biar gak gagal total pas alat baru).
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount FAILED");
  } else {
    Serial.println("LittleFS Mounted OK");
  }

  // ---------- SETUP PIN ----------
  dht.begin();
  pinMode(SOIL_PIN, INPUT);
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  // modul relay kebanyakan aktif LOW, jadi HIGH = mati (posisi aman di awal)
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  digitalWrite(RELAY3, HIGH);

  // ---------- KONEK WIFI ----------
  WiFi.mode(WIFI_STA); // mode "station" = ikut jaringan yang sudah ada
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi CONNECTED!");
  Serial.print(">> Buka dashboard di browser: http://");
  Serial.println(WiFi.localIP()); // INI IP yang dipakai buka dashboard

  // ---------- ROUTING WEB SERVER KE FILE DI LITTLEFS ----------
  // serveStatic(uri_yang_diminta_browser, filesystem, path_file_di_flash)
  // jadi kalau browser minta "/", dikasih isi "/index.html", dst.
  server.serveStatic("/", LittleFS, "/index.html");
  server.serveStatic("/index.html", LittleFS, "/index.html");
  server.serveStatic("/style.css", LittleFS, "/style.css");
  server.serveStatic("/script.js", LittleFS, "/script.js");

  // kalau ada request ke path yang gak dikenal, kasih 404 biasa
  server.onNotFound([]() {
    server.send(404, "text/plain", "File tidak ditemukan di LittleFS");
  });

  server.begin();       // nyalain web server-nya
  ThingSpeak.begin(client); // siapin library ThingSpeak pakai koneksi client di atas
}


void loop() {
  // WAJIB dipanggil tiap loop, ini yang bikin ESP32 "melayani"
  // request browser (buka halaman, dsb) — kalau ini gak dipanggil
  // terus-menerus, web server-nya bakal ngadat/lemot
  server.handleClient();

  // ---------- BACA SENSOR TIAP 2 DETIK ----------
  if (millis() - lastRead > 2000) {
    float nt = dht.readTemperature();
    float nh = dht.readHumidity();
    // DHT kadang gagal baca & balikin NaN, jadi kita cuma update
    // variabel global KALAU hasil bacanya valid — biar LCD/dashboard
    // gak tiba-tiba nampilin angka aneh pas sensor gagal sesaat
    if (!isnan(nt) && !isnan(nh)) {
      t = nt;
      h = nh;
    }

    soil = analogRead(SOIL_PIN);         // hasil mentah 0-4095
    soil = map(soil, 4095, 0, 0, 100);   // dikonversi ke persen (dibalik karena sensor kebanyakan "kering = angka besar")
    soil = constrain(soil, 0, 100);      // jaga-jaga biar gak keluar dari 0-100%

    Serial.print("T:"); Serial.print(t);
    Serial.print(" H:"); Serial.print(h);
    Serial.print(" Soil:"); Serial.println(soil);

    lastRead = millis();
  }

  // ---------- UPDATE LCD (jalan tiap loop, biar responsif) ----------
  lcd.setCursor(0, 0); lcd.print("T:"); lcd.print(t); lcd.print("C   ");
  lcd.setCursor(0, 1); lcd.print("H:"); lcd.print(h); lcd.print("%   ");
  lcd.setCursor(0, 2); lcd.print("Soil:"); lcd.print(soil); lcd.print("%   ");
  lcd.setCursor(0, 3); lcd.print(WiFi.localIP());
  // spasi ekstra di akhir tiap print() itu buat "menimpa" sisa
  // karakter lama kalau angka barunya lebih pendek (misal 100 -> 9)

  // ---------- LOGIKA RELAY (OTOMATIS + MANUAL SEKALIGUS) ----------
  // Relay1: nyala kalau SALAH SATU syarat ini kepenuhi:
  //   - suhu lebih dari 30°C (otomatis, dari sensor)
  //   - ATAU tombol Relay1 di dashboard lagi di-ON (manual)
  // Jadi walau suhu masih adem, lo tetep bisa nyalain paksa lewat web.
  digitalWrite(RELAY1, (t > 30 || relay1State == 1) ? LOW : HIGH);

  // Relay2: dari program awal lo emang gak ada syarat sensornya,
  // jadi ini murni ngikutin tombol dashboard aja (manual)
  digitalWrite(RELAY2, (relay2State == 1) ? LOW : HIGH);

  // Relay3: OTOMATIS, nyala kalau tanah kering (di bawah 30%)
  digitalWrite(RELAY3, (soil < 30) ? LOW : HIGH);

  // ---------- KIRIM DATA SENSOR KE THINGSPEAK TIAP 20 DETIK ----------
  if (millis() - lastSend > 20000) {
    ThingSpeak.setField(1, t);
    ThingSpeak.setField(2, h);
    ThingSpeak.setField(3, soil);
    int x = ThingSpeak.writeFields(channelID, writeAPIKey);
    // ThingSpeak versi gratis cuma terima update tiap >=15 detik per
    // channel, makanya intervalnya 20 detik biar aman gak kena tolak
    Serial.println(x == 200 ? "Upload OK" : "Upload FAIL");
    lastSend = millis();
  }

  // ---------- BACA STATUS RELAY DARI THINGSPEAK TIAP 20 DETIK ----------
  if (millis() - lastRelayPoll > 20000) {
    long r1 = ThingSpeak.readLongField(channelID, 4, readAPIKey);
    long r2 = ThingSpeak.readLongField(channelID, 5, readAPIKey);
    // cuma dipakai kalau request-nya sukses (status 200), biar kalau
    // internet lagi putus sesaat, relay gak keupdate ke nilai ngaco
    if (ThingSpeak.getLastReadStatus() == 200) {
      relay1State = (int)r1;
      relay2State = (int)r2;
    }
    lastRelayPoll = millis();
  }
}
