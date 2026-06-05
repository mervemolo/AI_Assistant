#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <driver/i2s.h> // Mikrofon için I2S kütüphanesi

// Mevcut Pinler
const int RED_PIN = 13;
const int GREEN_PIN = 16;
const int BLUE_PIN = 17;
const int PIR_PIN = 14;
const int SDA_PIN = 11;
const int SCL_PIN = 12;
const int LDR_PIN = 10;

// Yeni Mikrofon Test LEDleri
const int MIC_OK_LED = 3;    // YEŞİL (Çalışıyorsa)
const int MIC_ERR_LED = 4;   // KIRMIZI (Sessiz/Çalışmıyorsa)

// Mikrofon Pinleri
#define I2S_WS 6
#define I2S_SD 7
#define I2S_SCK 5
#define I2S_PORT I2S_NUM_0

// DHT Sensör Ayarı
#define DHTPIN 18
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

unsigned long hareketBaslangicZamani = 0;
bool hareketVarMi = false;

// I2S Mikrofon Kurulumu
void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}

void setColor(int r, int g, int b) {
  analogWrite(RED_PIN, r);
  analogWrite(GREEN_PIN, g);
  analogWrite(BLUE_PIN, b);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  dht.begin();
  setupI2S();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Ekran bulunamadi!");
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("MIKROFON & GUVENLIK");
  display.println("SISTEMI ACILIYOR...");
  display.display();

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  
  // Test LEDleri
  pinMode(MIC_OK_LED, OUTPUT);
  pinMode(MIC_ERR_LED, OUTPUT);

  setColor(0, 0, 0);
  delay(2000);
}

void loop() {
  // Mikrofon Verisi Okuma
  int32_t sample = 0;
  size_t bytes_read;
  i2s_read(I2S_PORT, &sample, sizeof(sample), &bytes_read, portMAX_DELAY);
  int sesSeviyesi = abs(sample) >> 16; 

  // MİKROFON LED TESTİ
  // Eğer ses seviyesi 300'den büyükse YEŞİL yanar, değilse KIRMIZI yanar.
  if (sesSeviyesi > 300) { 
    digitalWrite(MIC_OK_LED, HIGH); // Yeşil yan
    digitalWrite(MIC_ERR_LED, LOW);  // Kırmızı sön
  } else {
    digitalWrite(MIC_OK_LED, LOW);   // Yeşil sön
    digitalWrite(MIC_ERR_LED, HIGH); // Kırmızı yan
  }

  // Sensör verilerini oku
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int anlikOkuma = digitalRead(PIR_PIN);
  int isikDegeri = analogRead(LDR_PIN);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  if (isnan(h) || isnan(t)) display.print("HATA! ");
  else {
    display.print(t, 0); display.print("C %"); display.print(h, 0);
  }

  display.print(" LDR:"); display.print(isikDegeri);
  display.print(" MIC:"); display.println(sesSeviyesi);
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

  // Güvenlik Mantığı
  if (anlikOkuma == HIGH) {
    if (!hareketVarMi) { hareketVarMi = true; hareketBaslangicZamani = millis(); }
    unsigned long gecenSure = millis() - hareketBaslangicZamani;
    display.setCursor(0, 20);
    if (gecenSure > 2000) {
      setColor(255, 100, 0); // SARI
      display.setTextSize(2); display.println("UYARI!");
    } else {
      setColor(0, 255, 0); // YEŞİL
      display.setTextSize(2); display.println("HAREKET");
    }
  } else {
    hareketVarMi = false;
    setColor(255, 0, 0); // KIRMIZI
    display.setCursor(0, 25);
    display.setTextSize(2);
    display.println("GUVENLI");
  }

  display.display();
  delay(100); // Tepki hızını artırmak için bekleme süresini düşürdüm
}
