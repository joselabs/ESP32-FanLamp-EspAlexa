/*
 * ESP32 BLE to Alexa Bridge for Ceiling Fans (FanLamp / ZhiJia / Mikomika)
 * This code uses a stateless approach and FreeRTOS queues to bypass WiFi/BLE coexistence issues.
 * It requires 4-packet sequences (Frame Burst) to bypass the fan's hardware validation.
 */

#include <Arduino.h>
#include <WiFi.h>
#define ESPALEXA_ASYNC
#include <Espalexa.h>
#include <NimBLEDevice.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

Espalexa espalexa;
QueueHandle_t colaRadio;

// --- RAM STATE (Stateless Approach to save Flash memory) ---
bool luzRoom1Encendida = false;
bool luzRoom2Encendida = false;
uint8_t briRoom1 = 255;
uint8_t briRoom2 = 255;
uint16_t ctRoom1 = 326;
uint16_t ctRoom2 = 326;

EspalexaDevice* devLuzRoom1;
EspalexaDevice* devAireRoom1;
EspalexaDevice* devLuzRoom2;
EspalexaDevice* devAireRoom2;

// ==============================================================================
// ⬇️ REPLAY ATTACK HEX SEQUENCES (4 PACKETS PER COMMAND) ⬇️
// ==============================================================================

// ---------------------------------
// FAN 1 (Room 1)
// ---------------------------------
const char* ROOM1_LUZ_ON[4] = {
    "PASTE_HEX_PACKET_1_HERE",
    "PASTE_HEX_PACKET_2_HERE",
    "PASTE_HEX_PACKET_3_HERE",
    "PASTE_HEX_PACKET_4_HERE"
};
const char* ROOM1_LUZ_OFF[4] = {
    "PASTE_HEX_PACKET_1_HERE",
    "PASTE_HEX_PACKET_2_HERE",
    "PASTE_HEX_PACKET_3_HERE",
    "PASTE_HEX_PACKET_4_HERE"
};

const char* ROOM1_VEL_0[4] = {"...", "...", "...", "..."}; // Fan OFF
const char* ROOM1_VEL_1[4] = {"...", "...", "...", "..."};
const char* ROOM1_VEL_2[4] = {"...", "...", "...", "..."};
const char* ROOM1_VEL_3[4] = {"...", "...", "...", "..."};
const char* ROOM1_VEL_4[4] = {"...", "...", "...", "..."};
const char* ROOM1_VEL_5[4] = {"...", "...", "...", "..."};
const char* ROOM1_VEL_6[4] = {"...", "...", "...", "..."};

const char* ROOM1_BRI_MAS[4] = {"...", "...", "...", "..."};
const char* ROOM1_BRI_MEN[4] = {"...", "...", "...", "..."};
const char* ROOM1_TMP_CAL[4] = {"...", "...", "...", "..."};
const char* ROOM1_TMP_FRI[4] = {"...", "...", "...", "..."};

const char** ROOM1_VELOCIDADES[] = {ROOM1_VEL_0, ROOM1_VEL_1, ROOM1_VEL_2, ROOM1_VEL_3, ROOM1_VEL_4, ROOM1_VEL_5, ROOM1_VEL_6};


// ---------------------------------
// FAN 2 (Room 2)
// ---------------------------------
const char* ROOM2_LUZ_ON[4]  = {"...", "...", "...", "..."};
const char* ROOM2_LUZ_OFF[4] = {"...", "...", "...", "..."};

const char* ROOM2_VEL_0[4] = {"...", "...", "...", "..."}; // Fan OFF
const char* ROOM2_VEL_1[4] = {"...", "...", "...", "..."};
const char* ROOM2_VEL_2[4] = {"...", "...", "...", "..."};
const char* ROOM2_VEL_3[4] = {"...", "...", "...", "..."};
const char* ROOM2_VEL_4[4] = {"...", "...", "...", "..."};
const char* ROOM2_VEL_5[4] = {"...", "...", "...", "..."};
const char* ROOM2_VEL_6[4] = {"...", "...", "...", "..."};

const char* ROOM2_BRI_MAS[4] = {"...", "...", "...", "..."};
const char* ROOM2_BRI_MEN[4] = {"...", "...", "...", "..."};
const char* ROOM2_TMP_CAL[4] = {"...", "...", "...", "..."};
const char* ROOM2_TMP_FRI[4] = {"...", "...", "...", "..."};

const char** ROOM2_VELOCIDADES[] = {ROOM2_VEL_0, ROOM2_VEL_1, ROOM2_VEL_2, ROOM2_VEL_3, ROOM2_VEL_4, ROOM2_VEL_5, ROOM2_VEL_6};


// ==============================================================================
// ⚙️ RADIO FREQUENCY ENGINE (NimBLE)
// ==============================================================================

uint8_t char2hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

void emitirPaquete(const char* hexString) {
    uint8_t packet[31];
    for (int i = 0; i < 31; i++) {
        packet[i] = (char2hex(hexString[i * 2]) << 4) | char2hex(hexString[i * 2 + 1]);
    }

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->stop();
    delay(10); 
    
    NimBLEAdvertisementData data;
    data.addData((char*)packet, 31);
    pAdvertising->setAdvertisementData(data);
    pAdvertising->setMinInterval(32); 
    pAdvertising->setMaxInterval(32);
    
    pAdvertising->start();
    delay(100); 
    pAdvertising->stop();
}

void ejecutarSecuenciaCompleta(const char** secuencia) {
    Serial.println("  [RF] Executing 4-packet validation burst...");
    for (int i = 0; i < 4; i++) {
        if (String(secuencia[i]) == "...") continue; 
        emitirPaquete(secuencia[i]);
        delay(20); 
    }
    Serial.println("  [RF] Burst complete. Antenna released.");
}

// ==============================================================================
// 🧠 LOGIC & ALEXA CALLBACKS
// ==============================================================================

void procesarLuz(EspalexaDevice* dev, bool esRoom1, bool &estadoActual, uint8_t &estadoBri, uint16_t &estadoCt) {
    if (!dev) return;
    bool targetState = dev->getState();
    
    if (targetState != estadoActual) {
        if (targetState) {
            const char** seqON = esRoom1 ? ROOM1_LUZ_ON : ROOM2_LUZ_ON;
            xQueueSend(colaRadio, &seqON, 0);
        } else {
            const char** seqOFF = esRoom1 ? ROOM1_LUZ_OFF : ROOM2_LUZ_OFF;
            xQueueSend(colaRadio, &seqOFF, 0);
        }
        estadoActual = targetState;
    }

    if (targetState) {
        uint8_t targetBri = dev->getValue();
        int briDiff = (int)targetBri - (int)estadoBri;
        
        if (abs(briDiff) >= 15) { 
            int pasos = abs(briDiff) / 25; 
            if (pasos == 0) pasos = 1;
            const char** seqBri = (briDiff > 0) ? 
                (esRoom1 ? ROOM1_BRI_MAS : ROOM2_BRI_MAS) : 
                (esRoom1 ? ROOM1_BRI_MEN : ROOM2_BRI_MEN);
            for (int i = 0; i < pasos; i++) xQueueSend(colaRadio, &seqBri, 0);
            estadoBri = targetBri;
        }

        uint16_t targetCt = dev->getCt();
        int ctDiff = (int)targetCt - (int)estadoCt;

        if (abs(ctDiff) >= 25) {
            int pasosCt = abs(ctDiff) / 35;
            if (pasosCt == 0) pasosCt = 1;
            const char** seqCt = (ctDiff > 0) ? 
                (esRoom1 ? ROOM1_TMP_CAL : ROOM2_TMP_CAL) : 
                (esRoom1 ? ROOM1_TMP_FRI : ROOM2_TMP_FRI);
            for (int i = 0; i < pasosCt; i++) xQueueSend(colaRadio, &seqCt, 0);
            estadoCt = targetCt;
        }
    }
}

void procesarVentilador(EspalexaDevice* dev, bool esRoom1) {
    if (!dev) return;
    uint8_t targetVal = dev->getValue();
    const char** seqVel;
    
    if (targetVal == 0) {
        seqVel = esRoom1 ? ROOM1_VELOCIDADES[0] : ROOM2_VELOCIDADES[0];
    } else {
        uint8_t vel = (targetVal * 6) / 255;
        if (vel == 0) vel = 1;
        seqVel = esRoom1 ? ROOM1_VELOCIDADES[vel] : ROOM2_VELOCIDADES[vel];
    }
    xQueueSend(colaRadio, &seqVel, 0);
}

void cbLuzRoom1(EspalexaDevice* dev) { procesarLuz(dev, true, luzRoom1Encendida, briRoom1, ctRoom1); }
void cbLuzRoom2(EspalexaDevice* dev) { procesarLuz(dev, false, luzRoom2Encendida, briRoom2, ctRoom2); }
void cbAireRoom1(EspalexaDevice* dev) { procesarVentilador(dev, true); }
void cbAireRoom2(EspalexaDevice* dev) { procesarVentilador(dev, false); }

void setup() {
    Serial.begin(115200);
    delay(500);

    colaRadio = xQueueCreate(40, sizeof(const char**));

    WiFi.begin(ssid, password);
    Serial.print("Connecting WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");

    NimBLEDevice::init("");
    
    devLuzRoom1 = new EspalexaDevice("Room 1 Light", cbLuzRoom1, EspalexaDeviceType::whitespectrum);
    espalexa.addDevice(devLuzRoom1);
    devAireRoom1 = new EspalexaDevice("Room 1 Fan", cbAireRoom1, EspalexaDeviceType::dimmable);
    espalexa.addDevice(devAireRoom1);
    
    devLuzRoom2 = new EspalexaDevice("Room 2 Light", cbLuzRoom2, EspalexaDeviceType::whitespectrum);
    espalexa.addDevice(devLuzRoom2);
    devAireRoom2 = new EspalexaDevice("Room 2 Fan", cbAireRoom2, EspalexaDeviceType::dimmable);
    espalexa.addDevice(devAireRoom2);
    
    espalexa.begin();
    Serial.println("System ready. Waiting for Alexa commands...");
}

void loop() {
    espalexa.loop();
    
    const char** secuenciaPendiente;
    if (xQueueReceive(colaRadio, &secuenciaPendiente, 0) == pdTRUE) {
        ejecutarSecuenciaCompleta(secuenciaPendiente);
    }
    
    delay(5);
}
