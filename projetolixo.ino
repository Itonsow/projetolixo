// ESP32-CAM + HC-SR04 + Servo MG995 no IO13
// Aciona o servo quando distância <= 20 cm
// Ao detectar: sensor inativo por 3s, tira foto e envia via HTTP POST ao PC

#include <ESP32Servo.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ===== CONFIGURAÇÃO WiFi =====
const char* ssid     = "SEU_WIFI";
const char* password = "SUA_SENHA";

// ===== IP DO COMPUTADOR NA REDE =====
// Execute servidor_fotos.py no PC e coloque o IP que ele exibir aqui
const char* servidorIP = "http://192.168.1.100:5000/foto";

// ===== PINOS =====
#define TRIG_PIN        14
#define ECHO_PIN        15
#define SERVO_PIN       13
#define LED_BUILTIN_PIN  4   // Flash LED (ativo em HIGH)

// ===== PARÂMETROS =====
#define DISTANCIA_CM       20
#define INTERVALO_PISCA 10000   // ms
#define TEMPO_PAUSA      3000   // ms que o sensor fica inativo após detecção

#define SERVO_ABERTO    0
#define SERVO_FECHADO  90

// ===== CÂMERA - Pinout AI-Thinker ESP32-CAM =====
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

Servo servo;

unsigned long ultimoPisca     = 0;
unsigned long tempoDesativacao = 0;
bool sensorAtivo              = true;

// ===== INICIALIZAÇÃO DA CÂMERA =====
bool iniciarCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = CAM_PIN_D0;
  config.pin_d1       = CAM_PIN_D1;
  config.pin_d2       = CAM_PIN_D2;
  config.pin_d3       = CAM_PIN_D3;
  config.pin_d4       = CAM_PIN_D4;
  config.pin_d5       = CAM_PIN_D5;
  config.pin_d6       = CAM_PIN_D6;
  config.pin_d7       = CAM_PIN_D7;
  config.pin_xclk     = CAM_PIN_XCLK;
  config.pin_pclk     = CAM_PIN_PCLK;
  config.pin_vsync    = CAM_PIN_VSYNC;
  config.pin_href     = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn     = CAM_PIN_PWDN;
  config.pin_reset    = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA;   // 640x480
  config.jpeg_quality = 12;
  config.fb_count     = 1;

  return esp_camera_init(&config) == ESP_OK;
}

// ===== CAPTURA E ENVIO DA FOTO =====
void tirarEEnviarFoto() {
  // Descarta frame em buffer (foto antiga que ficou na fila do sensor)
  camera_fb_t* fb_descarte = esp_camera_fb_get();
  if (fb_descarte) esp_camera_fb_return(fb_descarte);

  // Acende flash 200ms para iluminar e captura frame fresco
  digitalWrite(LED_BUILTIN_PIN, HIGH);
  delay(200);

  camera_fb_t* fb = esp_camera_fb_get();

  digitalWrite(LED_BUILTIN_PIN, LOW);

  if (!fb) {
    Serial.println("[CÂMERA] Falha ao capturar foto.");
    return;
  }

  Serial.printf("[CÂMERA] Foto capturada: %u bytes. Enviando...\n", fb->len);

  HTTPClient http;
  http.begin(servidorIP);
  http.addHeader("Content-Type", "image/jpeg");

  int httpCode = http.POST(fb->buf, fb->len);

  if (httpCode == 200) {
    Serial.println("[HTTP] Foto enviada e salva com sucesso!");
  } else {
    Serial.printf("[HTTP] Falha no envio. Código: %d\n", httpCode);
  }

  http.end();
  esp_camera_fb_return(fb);
}

// ===== SENSOR ULTRASSÔNICO =====
long medirDistanciaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duracao = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duracao == 0) return -1;
  return duracao * 0.0343 / 2;
}

void piscarBuiltin() {
  digitalWrite(LED_BUILTIN_PIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN_PIN, LOW);
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_BUILTIN_PIN, OUTPUT);
  digitalWrite(LED_BUILTIN_PIN, LOW);

  servo.attach(SERVO_PIN);
  servo.write(SERVO_FECHADO);

  if (iniciarCamera()) {
    Serial.println("[CÂMERA] Inicializada com sucesso.");
  } else {
    Serial.println("[CÂMERA] ERRO ao inicializar.");
  }

  Serial.printf("[WiFi] Conectando a %s...\n", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Conectado! IP do ESP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[HTTP] Fotos serão enviadas para: %s\n", servidorIP);
}

// ===== LOOP =====
void loop() {
  unsigned long agora = millis();

  // Pisca LED a cada 10 segundos (heartbeat)
  if (agora - ultimoPisca >= INTERVALO_PISCA) {
    ultimoPisca = agora;
    piscarBuiltin();
  }

  if (!sensorAtivo) {
    // Aguarda fim da pausa de 3 segundos
    if (agora - tempoDesativacao >= TEMPO_PAUSA) {
      sensorAtivo = true;
      Serial.println("[SENSOR] Reativado.");
      servo.write(SERVO_FECHADO);
    }
    delay(10);
    return;
  }

  // Leitura do sensor
  long distancia = medirDistanciaCm();

  if (distancia > 0) {
    Serial.printf("[SENSOR] Distância: %ld cm\n", distancia);

    if (distancia <= DISTANCIA_CM) {
      Serial.println("[SENSOR] Objeto detectado! Abrindo servo...");
      servo.write(SERVO_ABERTO);

      // Desativa sensor e registra o momento
      sensorAtivo      = false;
      tempoDesativacao = millis();

      // Tira e envia a foto durante os 3 segundos de pausa
      tirarEEnviarFoto();
    }
  } else {
    Serial.println("[SENSOR] Sem leitura (fora de alcance).");
    servo.write(SERVO_FECHADO);
  }

  delay(100);
}