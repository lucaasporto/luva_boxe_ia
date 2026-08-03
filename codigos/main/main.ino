#include <projeto_luva_v2_inferencing.h>
#include <Arduino_LSM9DS1.h>
#include <ArduinoBLE.h>

// --- CONFIGURAÇÃO DO BLUETOOTH (Serviço UART/Serial padrão Nordic) ---
BLEService uartService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"); 

// Característica RX (Recebe dados do App) - Adicionada para o app reconhecer o perfil!
BLEStringCharacteristic rxChar("6E400002-B5A3-F393-E0A9-E50E24DCCA9E", BLEWrite, 256); 

// Característica TX (Envia os golpes para o App)
BLEStringCharacteristic txChar("6E400003-B5A3-F393-E0A9-E50E24DCCA9E", BLERead | BLENotify, 256);

// O Buffer global
float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

// Contadores
int contadoresGolpes[EI_CLASSIFIER_LABEL_COUNT];

// Configurações da Janela
const int LEITURAS_POR_TURNO = 10;
const int DADOS_POR_TURNO = LEITURAS_POR_TURNO * 6;

const char* ESTADOS_PARADOS[] = {"neutro", "guarda"};
const int NUM_ESTADOS_PARADOS = sizeof(ESTADOS_PARADOS) / sizeof(ESTADOS_PARADOS[0]);

bool ehEstadoParado(const String &label) {
  for (int i = 0; i < NUM_ESTADOS_PARADOS; i++) {
    if (label == ESTADOS_PARADOS[i]) return true;
  }
  return false;
}

const int CONFIRMACOES_NECESSARIAS = 2;
String candidatoLabel = "";
int candidatoContagem = 0;
String labelConfirmadoAnterior = "parado";

void setup() {
  Serial.begin(115200);

  if (!IMU.begin()) {
    Serial.println("Falha ao inicializar o IMU!");
    while (1);
  }

  // --- INICIALIZA O BLUETOOTH ---
  if (!BLE.begin()) {
    Serial.println("Falha ao iniciar o Bluetooth!");
    while (1);
  }
  
  BLE.setLocalName("LuvaBoxe"); // Nome que vai aparecer no PC/Telemóvel
  BLE.setAdvertisedService(uartService);
  uartService.addCharacteristic(rxChar);
  uartService.addCharacteristic(txChar);
  
  BLE.addService(uartService);
  BLE.advertise();
  Serial.println("Bluetooth ativado! Aguardando conexões...");
  // ------------------------------

  if (EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME != 6) {
    Serial.println("ERRO: Modelo treinado com número diferente de eixos!");
    while (1);
  }

  for (size_t i = 0; i < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE; i++) {
    features[i] = 0.0f;
  }
  for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    contadoresGolpes[i] = 0;
  }
}

void loop() {
  // Mantém a conexão BLE ativa
  BLEDevice central = BLE.central();

  // 1. DESLIZA A JANELA
  for (size_t i = 0; i < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - DADOS_POR_TURNO; i++) {
    features[i] = features[i + DADOS_POR_TURNO];
  }

  // 2. GRAVA O PRESENTE
  size_t index_inicio = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - DADOS_POR_TURNO;
  for (size_t i = 0; i < LEITURAS_POR_TURNO; i++) {
    unsigned long startMillis = millis();
    float ax, ay, az, gx, gy, gz;
    
    if (IMU.accelerationAvailable() && IMU.gyroscopeAvailable()) {
      IMU.readAcceleration(ax, ay, az);
      IMU.readGyroscope(gx, gy, gz);
    }

    size_t offset = index_inicio + (i * 6);
    features[offset + 0] = ax;
    features[offset + 1] = ay;
    features[offset + 2] = az;
    features[offset + 3] = gx;
    features[offset + 4] = gy;
    features[offset + 5] = gz;

    while (millis() - startMillis < 10) {}
  }

  // 3. PROCESSA A IA
  signal_t signal;
  numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
  if (err != EI_IMPULSE_OK) return;

  // 4. RESULTADO BRUTO DESTA JANELA
  float maior_certeza = 0;
  int indice_vencedor = -1;
  for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > maior_certeza) {
      maior_certeza = result.classification[i].value;
      indice_vencedor = i;
    }
  }

  String labelBruto = (maior_certeza > 0.85 && indice_vencedor >= 0)
                         ? String(result.classification[indice_vencedor].label)
                         : "incerto";

  // 5. CONFIRMAÇÃO RÁPIDA
  if (labelBruto == candidatoLabel) {
    candidatoContagem++;
  } else {
    candidatoLabel = labelBruto;
    candidatoContagem = 1;
  }

  if (candidatoContagem < CONFIRMACOES_NECESSARIAS) return;

  String labelConfirmado = candidatoLabel;
  bool labelMudou = (labelConfirmado != labelConfirmadoAnterior);
  bool ehGolpeNovo = labelMudou && !ehEstadoParado(labelConfirmado) && labelConfirmado != "incerto";

  if (ehGolpeNovo) {
    // Comando ANSI para limpar a tela e mover o cursor para o topo
    String comandoLimpar = "\x1B[2J\x1B[H";

    // Monta o texto que será enviado por cabo E por Bluetooth (já com o comando de limpar no início)
    String relatorio = comandoLimpar + "\n=====================================\n";
    relatorio += "🥊 NOVO GOLPE: >>> " + labelConfirmado + " <<<\n";
    relatorio += "-------------------------------------\n";
    relatorio += "          PLACAR DE GOLPES           \n";
    
    // Atualiza o contador do golpe atual
    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
      if (labelConfirmado == String(result.classification[i].label)) {
        contadoresGolpes[i]++;
        break;
      }
    }

    // Adiciona todos os contadores ao relatório (ignorando os estados parados)
    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
      String nomeLabel = String(result.classification[i].label);
      if (!ehEstadoParado(nomeLabel)) {
        relatorio += " - " + nomeLabel + ": " + String(contadoresGolpes[i]) + "\n";
      }
    }
    relatorio += "=====================================\n";

    // Mostra no PC via Cabo (Monitor Serial) usando print em vez de println
    Serial.print(relatorio);

    // Envia para o PC/Celular via Bluetooth (se estiver conectado)
    if (central && central.connected()) {
      txChar.writeValue(relatorio);
    }
  }

  labelConfirmadoAnterior = labelConfirmado;
}