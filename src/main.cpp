#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <driver/i2s.h>
#include <math.h>

// ================= PIN TANIMLAMALARI =================

// RGB LED Pinleri
const int RED_PIN = 13;
const int GREEN_PIN = 16;
const int BLUE_PIN = 17;

// Sensör Pinleri
const int PIR_PIN = 14;
const int LDR_PIN = 10;
const int TOUCH_PIN = 8;
#define DHTPIN 18
#define DHTTYPE DHT11

// I2C Ekran Pinleri
const int SDA_PIN = 11;
const int SCL_PIN = 12;

// Mikrofon Test LED'leri
const int MIC_OK_LED = 3;
const int MIC_ERR_LED = 4;

// I2S Mikrofon Pinleri (RX)
#define MIC_I2S_WS 6
#define MIC_I2S_SD 7
#define MIC_I2S_SCK 5
#define I2S_MIC_PORT I2S_NUM_0

// I2S Hoparlör/Buzzer Pinleri (TX)
#define SPK_I2S_DOUT 42
#define SPK_I2S_BCLK 2
#define SPK_I2S_LRC 1
#define I2S_SPEAKER_PORT I2S_NUM_1

// Ekran Değiştirme Butonu
#define BUTTON_PIN 41

// ================= AYARLAR & EŞİK DEĞERLERİ =================

#define SAMPLE_RATE 44100

// Geliştirilmiş Mikrofon Eşikleri (Histerezis)
const int MIC_HIGH_THRESHOLD = 600;
const int MIC_LOW_THRESHOLD = 150;
bool micLedState = false;

// DÜZELTİLEN LDR GÜNDÜZ/GECE EŞİĞİ
// Işık arttıkça değer düşer. 1500'ün altı aydınlık (Gündüz), üstü karanlık (Gece) kabul edilir.
const int LDR_THRESHOLD = 200;

// Notalar
#define DO 262
#define RE 294
#define MI 330
#define FA 349
#define SOL 392
#define LA 440
#define SI 494
#define SOLS 415
#define RES 311

struct Note
{
    int freq;
    int time;
};

Note song[] = {
    {MI, 200}, {RES, 200}, {MI, 400}, {SI, 200}, {LA, 200}, {SI, 400}, {SOLS, 200}, {FA, 200}, {SOLS, 400}, {SOL, 400}, {FA, 400}, {MI, 400}, {FA, 400}, {FA, 200}, {MI, 200}, {RES, 400}, {RES, 400}, {FA, 400}, {MI, 400}, {SOLS, 200}, {LA, 400}, {SOLS, 200}, {LA, 400}, {SI, 400}, {MI, 200}, {RES, 200}, {MI, 400}, {SI, 200}, {LA, 200}, {SI, 400}, {SOLS, 200}, {FA, 200}, {SOLS, 400}, {SOL, 400}, {FA, 400}, {MI, 400}};

int songSize = sizeof(song) / sizeof(song[0]);

// ================= GLOBAL DEĞİŞKENLER =================

volatile bool playing = false;
int noteIndex = 0;
unsigned long lastButton = 0;
int screenMode = 0; // 0: Dashboard (Normal Ekran), 1: Yüz İfadeleri Ekranı

unsigned long hareketBaslangicZamani = 0;
bool hareketVarMi = false;
int sonTouchDurum = LOW;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);

// ================= SÜRÜCÜ KURULUMLARI =================

void setupI2SMic()
{
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false};

    i2s_pin_config_t pin_config = {
        .bck_io_num = MIC_I2S_SCK,
        .ws_io_num = MIC_I2S_WS,
        .data_out_num = -1,
        .data_in_num = MIC_I2S_SD};

    i2s_driver_install(I2S_MIC_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_MIC_PORT, &pin_config);
}

void setupI2SSpeaker()
{
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0};

    i2s_pin_config_t pin = {
        .bck_io_num = SPK_I2S_BCLK,
        .ws_io_num = SPK_I2S_LRC,
        .data_out_num = SPK_I2S_DOUT,
        .data_in_num = -1};

    i2s_driver_install(I2S_SPEAKER_PORT, &cfg, 0, NULL);
    i2s_set_pin(I2S_SPEAKER_PORT, &pin);
}

// ================= ARKA PLAN MÜZİK GÖREVİ =================

void playTone(int freq, int duration)
{
    int samples = SAMPLE_RATE * duration / 1000;
    int16_t buffer[256];

    for (int i = 0; i < samples; i += 256)
    {
        if (!playing)
            return;

        for (int j = 0; j < 256; j++)
        {
            float t = (float)(i + j) / SAMPLE_RATE;
            float wave = sin(2 * PI * freq * t);
            buffer[j] = wave * 14000;
        }

        size_t written;
        i2s_write(I2S_SPEAKER_PORT, buffer, sizeof(buffer), &written, portMAX_DELAY);
    }
}

void musicTask(void *pvParameters)
{
    while (1)
    {
        if (playing)
        {
            playTone(song[noteIndex].freq, song[noteIndex].time);
            noteIndex++;
            if (noteIndex >= songSize)
            {
                noteIndex = 0;
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void setColor(int r, int g, int b)
{
    analogWrite(RED_PIN, r);
    analogWrite(GREEN_PIN, g);
    analogWrite(BLUE_PIN, b);
}

// ================= YÜZ İFADELERİ ÇİZİM FONKSİYONU =================

void drawFace(String status, String ldrMode)
{
    display.clearDisplay();

    if (status == "TOUCH")
    {
        // Gözler: Neşeli kısık gözler (^ ^)
        display.drawLine(30, 25, 40, 15, SSD1306_WHITE);
        display.drawLine(40, 15, 50, 25, SSD1306_WHITE);
        display.drawLine(78, 25, 88, 15, SSD1306_WHITE);
        display.drawLine(88, 15, 98, 25, SSD1306_WHITE);
        // Ağız: Büyük açık mutlu ağız
        display.fillTriangle(54, 40, 74, 40, 64, 55, SSD1306_WHITE);
    }
    else if (status == "ALARM")
    {
        // Gözler: Fal taşı gibi açılmış şaşkın/korkmuş gözler (o o)
        display.drawCircle(40, 22, 10, SSD1306_WHITE);
        display.fillCircle(40, 22, 3, SSD1306_WHITE);
        display.drawCircle(88, 22, 10, SSD1306_WHITE);
        display.fillCircle(88, 22, 3, SSD1306_WHITE);
        // Ağız: Şaşkın 'O' ifadesi
        display.drawCircle(64, 48, 7, SSD1306_WHITE);
    }
    else if (ldrMode == "GECE")
    {
        // Gözler: Uykulu kapalı gözler (- -)
        display.drawLine(30, 25, 50, 25, SSD1306_WHITE);
        display.drawLine(78, 25, 98, 25, SSD1306_WHITE);
        // Ağız: Küçük düz çizgi veya hafif uyku horlaması
        display.drawLine(58, 45, 70, 45, SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(105, 10);
        display.print("z");
        display.setCursor(112, 5);
        display.print("Z");
    }
    else
    {
        // Normal / Mutlu Durum
        // Gözler: Standart sevimli gözler
        display.fillCircle(40, 22, 6, SSD1306_WHITE);
        display.fillCircle(88, 22, 6, SSD1306_WHITE);
        // Ağız: Klasik tebessüm
        display.drawRoundRect(52, 42, 24, 10, 4, SSD1306_WHITE);
        display.fillRect(52, 40, 24, 5, SSD1306_BLACK); // Üst kısmını silerek yay yapıyoruz
    }
}

// ================= SETUP =================

void setup()
{
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);
    dht.begin();

    setupI2SMic();
    setupI2SSpeaker();

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
        Serial.println("Ekran bulunamadi!");
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 10);
    display.setTextSize(1);
    display.println("SISTEM ACILIYOR...");
    display.println("EKRAN MODU DESTEGI");
    display.display();

    // Pin Modları
    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BLUE_PIN, OUTPUT);
    pinMode(PIR_PIN, INPUT);
    pinMode(LDR_PIN, INPUT);
    pinMode(TOUCH_PIN, INPUT);
    pinMode(BUTTON_PIN, INPUT);
    pinMode(MIC_OK_LED, OUTPUT);
    pinMode(MIC_ERR_LED, OUTPUT);

    setColor(0, 0, 0);
    delay(2000);

    xTaskCreatePinnedToCore(musicTask, "MusicTask", 4096, NULL, 1, NULL, 0);
}

// ================= MAIN LOOP =================

void loop()
{
    // 1. BUTON KONTROLÜ (Ekran Değiştirme)
    if (digitalRead(BUTTON_PIN) == HIGH && (millis() - lastButton > 400))
    {
        screenMode = (screenMode == 0) ? 1 : 0; // 0 ise 1 yapar, 1 ise 0 yapar.
        lastButton = millis();
        Serial.print("Ekran Modu Degisti: ");
        Serial.println(screenMode);
    }

    // 2. DOKUNMATİK SENSÖR KONTROLÜ (Müzik Aç/Kapat & Durum)
    int touchDurum = digitalRead(TOUCH_PIN);
    if (touchDurum == HIGH && sonTouchDurum == LOW)
    {
        playing = !playing; // Dokunulduğu an müziği tersine çevirir (Play/Pause)
        Serial.println(playing ? "Muzik ACILDI" : "Muzik KAPATILDI");
    }
    sonTouchDurum = touchDurum;

    // 3. MİKROFON KONTROLÜ (Histerezis Filtreli)
    int32_t sample = 0;
    size_t bytes_read;
    i2s_read(I2S_MIC_PORT, &sample, sizeof(sample), &bytes_read, portMAX_DELAY);
    int sesSeviyesi = abs(sample) >> 16;

    if (sesSeviyesi > MIC_HIGH_THRESHOLD)
    {
        micLedState = true;
    }
    else if (sesSeviyesi < MIC_LOW_THRESHOLD)
    {
        micLedState = false;
    }

    if (micLedState)
    {
        digitalWrite(MIC_OK_LED, HIGH);
        digitalWrite(MIC_ERR_LED, LOW);
    }
    else
    {
        digitalWrite(MIC_OK_LED, LOW);
        digitalWrite(MIC_ERR_LED, HIGH);
    }

    // 4. SENSÖR VERİLERİ OKUMA
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int anlikOkuma = digitalRead(PIR_PIN);
    int isikDegeri = analogRead(LDR_PIN);

    // DÜZELTİLEN LDR MANTIĞI
    String ldrDurum = (isikDegeri < LDR_THRESHOLD) ? "GECE" : "GUNDUZ";

    // 5. GÜVENLİK VE RGB LED AYARLARI (Arka planda hep çalışır)
    String genelDurum = "NORMAL";

    if (touchDurum == HIGH)
    {
        setColor(0, 0, 255); // MAVİ
        genelDurum = "TOUCH";
    }
    else if (anlikOkuma == HIGH)
    {
        if (!hareketVarMi)
        {
            hareketVarMi = true;
            hareketBaslangicZamani = millis();
        }
        unsigned long gecenSure = millis() - hareketBaslangicZamani;

        if (gecenSure > 2000)
        {
            setColor(255, 100, 0); // SARI
            genelDurum = "ALARM";
        }
        else
        {
            setColor(0, 255, 0); // YEŞİL
            genelDurum = "HAREKET";
        }
    }
    else
    {
        hareketVarMi = false;
        setColor(255, 0, 0); // KIRMIZI
        genelDurum = "NORMAL";
    }

    // 6. EKRAN MODUNA GÖRE ÇIKTI VERME
    if (screenMode == 1)
    {
        // YÜZ İFADELERİ EKRANI
        drawFace(genelDurum, ldrDurum);
    }
    else
    {
        // DASHBOARD (MEVCUT EKRAN GÖRÜNTÜSÜ)
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);

        if (isnan(h) || isnan(t))
            display.print("HATA! ");
        else
        {
            display.print(t, 0);
            display.print("C %");
            display.print(h, 0);
        }
        display.print(" | ");
        display.println(ldrDurum);

        display.setCursor(0, 10);
        display.print("MIC V:");
        display.print(sesSeviyesi);
        display.print(" | TCH:");
        display.println(touchDurum == HIGH ? "1" : "0");

        display.drawLine(0, 20, 128, 20, SSD1306_WHITE);

        display.setCursor(0, 28);
        display.setTextSize(2);
        if (genelDurum == "TOUCH")
            display.println("DOKUNULDU");
        else if (genelDurum == "ALARM")
            display.println("UYARI!");
        else if (genelDurum == "HAREKET")
            display.println("HAREKET");
        else
            display.println("GUVENLI");

        display.setTextSize(1);
        display.setCursor(0, 55);
        display.print("Muzik: ");
        display.println(playing ? "CALIYOR" : "DURDURULDU");
    }

    display.display();
    delay(100);
}