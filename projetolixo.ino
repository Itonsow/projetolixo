/*
  ESP32-CAM + HC-SR04

  Funcionamento:
  1. Conecta o ESP32-CAM ao Wi-Fi.
  2. Mede a distancia usando o HC-SR04.
  3. Quando a distancia fica abaixo de 10 cm, tira uma foto.
  4. Envia a foto por HTTP POST para o servidor Python no computador.

  Ligacoes do HC-SR04:
  - TRIG -> GPIO 14
  - ECHO -> GPIO 15, passando por divisor de tensao/level shifter para 3.3V
  - VCC  -> 5V externo
  - GND  -> GND da fonte externa e GND do ESP32-CAM em comum
*/

#include "esp_camera.h"
#include <HTTPClient.h>
#include <WiFi.h>

// ===== Wi-Fi =====
const char* WIFI_SSID = "Ximenes";
const char* WIFI_PASSWORD = "ximenes1234";

// Rode o servidor_fotos.py no computador e copie a URL exibida aqui.
// Exemplo: http://192.168.0.25:5000/foto
const char* SERVIDOR_FOTOS_URL = "http://172.16.225.61:5000/foto";

// ===== Sensor ultrassonico =====
#define TRIG_PIN 14
#define ECHO_PIN 15
#define DISTANCIA_GATILHO_CM 10.0f
#define TIMEOUT_ECHO_US 30000UL

// ===== Camera / flash =====
#define FLASH_LED_PIN 4
#define TEMPO_FLASH_MS 180

// Evita tirar varias fotos seguidas do mesmo objeto parado.
#define INTERVALO_MINIMO_FOTOS_MS 5000UL

// ===== Pinout AI-Thinker ESP32-CAM =====
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

unsigned long ultimaFotoMs = 0;

bool iniciarCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA; // 320x240
    config.jpeg_quality = 14;
    config.fb_count = 1;
  }

  esp_err_t erro = esp_camera_init(&config);
  if (erro != ESP_OK) {
    Serial.printf("[CAMERA] Falha ao iniciar. Erro: 0x%x\n", erro);
    return false;
  }

  Serial.println("[CAMERA] Inicializada.");
  return true;
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.printf("[WIFI] Conectando em %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.printf("[WIFI] Conectado. IP do ESP32-CAM: %s\n", WiFi.localIP().toString().c_str());
}

float medirDistanciaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duracao = pulseIn(ECHO_PIN, HIGH, TIMEOUT_ECHO_US);
  if (duracao == 0) {
    return -1.0f;
  }

  // Velocidade aproximada do som: 0,0343 cm/us.
  return (duracao * 0.0343f) / 2.0f;
}

camera_fb_t* capturarFoto() {
  // Descarta um frame antigo, se existir, para reduzir chance de enviar foto atrasada.
  camera_fb_t* descarte = esp_camera_fb_get();
  if (descarte) {
    esp_camera_fb_return(descarte);
  }

  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(TEMPO_FLASH_MS);

  camera_fb_t* foto = esp_camera_fb_get();

  digitalWrite(FLASH_LED_PIN, LOW);

  if (!foto) {
    Serial.println("[CAMERA] Nao foi possivel capturar a foto.");
  }

  return foto;
}

bool enviarFoto(camera_fb_t* foto) {
  if (!foto) {
    return false;
  }

  conectarWiFi();

  HTTPClient http;
  http.begin(SERVIDOR_FOTOS_URL);
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(10000);

  Serial.printf("[HTTP] Enviando foto com %u bytes...\n", (unsigned int)foto->len);
  int codigoHttp = http.POST(foto->buf, foto->len);
  String resposta = http.getString();
  http.end();

  if (codigoHttp == 200) {
    Serial.printf("[HTTP] Foto salva pelo servidor: %s\n", resposta.c_str());
    return true;
  }

  Serial.printf("[HTTP] Falha no envio. Codigo: %d Resposta: %s\n", codigoHttp, resposta.c_str());
  return false;
}

void tirarEEnviarFoto() {
  camera_fb_t* foto = capturarFoto();
  bool enviada = enviarFoto(foto);

  if (foto) {
    esp_camera_fb_return(foto);
  }

  if (enviada) {
    ultimaFotoMs = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(FLASH_LED_PIN, LOW);

  iniciarCamera();
  conectarWiFi();

  Serial.printf("[HTTP] Servidor de fotos: %s\n", SERVIDOR_FOTOS_URL);
  Serial.printf("[SENSOR] Disparo configurado para distancia menor que %.1f cm.\n", DISTANCIA_GATILHO_CM);
}

void loop() {
  float distancia = medirDistanciaCm();

  if (distancia < 0) {
    Serial.println("[SENSOR] Sem leitura.");
    delay(250);
    return;
  }

  Serial.printf("[SENSOR] Distancia: %.1f cm\n", distancia);

  bool perto = distancia < DISTANCIA_GATILHO_CM;
  bool intervaloOk = millis() - ultimaFotoMs >= INTERVALO_MINIMO_FOTOS_MS;

  if (perto && intervaloOk) {
    Serial.println("[SENSOR] Objeto detectado abaixo de 10 cm. Capturando foto...");
    tirarEEnviarFoto();
  }

  delay(250);
}
