#include <Wire.h>
#include <BH1750.h>

// ================= USER CONFIGURATION =================
const char* DEVICE_NAME = "STATION_A";  // <--- CHANGE to "STATION_B" for the second board
// ======================================================

// Pin Definitions
#define TX_PIN 16
#define RX_PIN 35

// Serial Settings
#define BAUD_PC 115200    // Speed to Computer (USB)
#define BAUD_LIFI 19200   // Speed over Light (Physical Layer)

// Hardware Objects
BH1750 lightMeter;

// Global Variables
unsigned long lastTelemetry = 0;
const int TELEMETRY_INTERVAL = 500; // Send stats every 500ms

// Statistics Tracking
float noiseFloor = 20.0;
long totalPackets = 0;
long errorPackets = 0;
long fecFixedPackets = 0;

// Adaptive Speed Control Variable
int txDelay = 0; // Microseconds of delay between characters

void setup() {
  // 1. Initialize Serial Ports
  Serial.begin(BAUD_PC);
  Serial2.begin(BAUD_LIFI, SERIAL_8N1, RX_PIN, TX_PIN);

  // 2. Initialize I2C and Sensor
  Wire.begin();
  delay(500); // Allow sensor to power up
  Serial.println("\n--- LI-FI SYSTEM ONLINE (NOISE IMMUNITY MODE) ---");
  Serial.print("[ID] ROLE: "); Serial.println(DEVICE_NAME);
  
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("[INFO] Sensor: ONLINE");
  } else {
    Serial.println("[INFO] Sensor: ERROR (Check Wiring)");
  }
}

// Helper: Calculate simple ASCII checksum
int calculateChecksum(String s) {
  int sum = 0;
  for(int i=0; i<s.length(); i++) sum += s[i];
  return sum;
}

void loop() {
  unsigned long now = millis();

  // ==================================================
  // 1. TELEMETRY & ADAPTIVE LOGIC TASK (Every 500ms)
  // ==================================================
  if (now - lastTelemetry > TELEMETRY_INTERVAL) {
    float lux = lightMeter.readLightLevel();
    if (lux < 0) lux = 0;

    // --- "REVERSED" ADAPTIVE LOGIC (Noise Immunity) ---
    // High Light = High Interference -> Slow Down
    // Low Light  = Clean Air        -> Speed Up
    String mode;

    if (lux > 1000) {
      mode = "ROBUST (High Noise)";
      txDelay = 2000; // Add 2ms delay per character
    }
    else if (lux > 200) {
      mode = "BALANCED";
      txDelay = 500;  // Add 0.5ms delay per character
    }
    else {
      mode = "TURBO (Clean Air)";
      txDelay = 0;    // Full Speed (No added delay)
    }

    // Calculate SNR (Signal to Noise Ratio)
    float signal = lux;
    if (signal < noiseFloor) signal = noiseFloor;
    float snr = 20 * log10(signal / noiseFloor);

    // Calculate BER (Bit Error Rate - estimated by packet loss)
    float ber = 0.0;
    if (totalPackets > 0) ber = (float)errorPackets / (float)totalPackets;

    // Send Data Packet to Dashboard
    // Format: [DATA] NAME|LUX|SNR|BER|MODE|FEC_COUNT
    // Pinpoints performance mapping parameters
    Serial.printf("[DATA] %s|%.1f|%.1f|%.4f|%s|%ld\n",
                  DEVICE_NAME, lux, snr, ber, mode.c_str(), fecFixedPackets);

    lastTelemetry = now;
  }

  // ==================================================
  // 2. TRANSMIT TASK (USB -> Li-Fi) with FEC & DELAY
  // ==================================================
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    if(msg.length() > 0) {

      // A. Construct FEC Packet: <MSG | CHECKSUM | MSG_COPY>
      int chk = calculateChecksum(msg);
      String packet = "<" + msg + "|" + String(chk) + "|" + msg + ">";

      // B. Send Character-by-Character with Adaptive Delay
      for (int i = 0; i < packet.length(); i++) {
        Serial2.write(packet[i]);
        if (txDelay > 0) {
          delayMicroseconds(txDelay);
        }
      }
      Serial2.write('\n'); // End of packet

      // C. Echo to Dashboard
      Serial.println("You Sent: " + msg);
    }
  }

  // ==================================================
  // 3. RECEIVE TASK (Li-Fi -> USB) with DECODING
  // ==================================================
  if (Serial2.available()) {
    String packet = Serial2.readStringUntil('\n');
    packet.trim();

    // Validate Packet Wrapper < ... >
    if (packet.startsWith("<") && packet.endsWith(">")) {
      packet = packet.substring(1, packet.length() - 1); // Remove < >

      // Find Separators
      int firstPipe = packet.indexOf('|');
      int lastPipe = packet.lastIndexOf('|');

      if (firstPipe > 0 && lastPipe > firstPipe) {
        // Extract Parts
        String mainMsg = packet.substring(0, firstPipe);
        String chkStr = packet.substring(firstPipe + 1, lastPipe);
        String backupMsg = packet.substring(lastPipe + 1);

        // Verify Checksums
        int rxChecksum = chkStr.toInt();
        int mainCalc = calculateChecksum(mainMsg);
        int backupCalc = calculateChecksum(backupMsg);

        totalPackets++;

        // --- ERROR CORRECTION LOGIC ---
        if (mainCalc == rxChecksum) {
          // Plan A: Main message is healthy
          Serial.println("RX: " + mainMsg);
        }
        else if (backupCalc == rxChecksum) {
          // Plan B: Main failed, but Backup is healthy!
          fecFixedPackets++;
          Serial.println("RX: " + backupMsg + " [FEC FIXED]");
        }
        else {
          // Plan C: Both corrupted
          errorPackets++;
          // ACTIVE ERROR REPORTING FOR DASHBOARD
          Serial.println("[ERROR] Checksum Mismatch (Data Corrupted)");
        }
      }
    }
    // Note: We silently ignore raw noise that doesn't start with <
  }
}