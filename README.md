# Bidirectional-Adaptive-LI-FI-System
Advanced Bidirectional Li-Fi Communication System With Encryption, Adaptive Speed Control And Error Correction 

![alt text](<WhatsApp Image 2026-06-24 at 11.16.22 PM.jpeg>)

---

### Project Overview

The project implements a smart, bidirectional Visible Light Communication (VLC / Li-Fi) system consisting of two identical transceiver stations. Built to overcome the limitations of static Li-Fi systems that fail under varying ambient light or distance, this system features an environmental awareness layer. It decouples high-speed data processing from environmental tracking by utilizing the dual-core architecture of the ESP32 microcontroller, ensuring real-time adaptive communication.

---

### 1. Hardware Architecture

The hardware layer is designed to safely handle high-power optical transmission while maintaining high sensitivity for signal reception.

* **Processing & Core Allocation:** The system centers on the ESP32 microcontroller running at 240 MHz. To prevent slower sensor cycles from disrupting high-speed communication lines, Core 0 is dedicated to slow tasks (telemetry and sensor readings) and Core 1 is dedicated to fast tasks (signal processing).
* **Transmitter Circuit:** 1.  **Light Source:** A high-power 5-Watt Chip-on-Board (COB) White LED simulates a real lighting fixture and provides long-range illumination.
2.  **Driver Setup:** Because the ESP32 pins cannot supply the ~1000mA required by the LED, a Low-Side Switching circuit is built using an N-Channel MOSFET.
3.  **Protection & Thermal Control:** A 100-ohm resistor is placed at the MOSFET gate to limit in-rush current. The LED is mounted on an aluminum heatsink with thermal paste to mitigate thermal droop.
* **Receiver Circuit:** 1.  **Photodiode Configuration:** A BPW34 PIN photodiode is reverse-biased in a photoconductive setup to lower junction capacitance and maximize switching speed.
2.  **Gain and Bandwidth Balance:** A load resistor optimized to approximately 100k Ohms balances the voltage swing (gain) with pulse responsiveness.
* **Environmental Sensor:** A BH1750 digital ambient light sensor connects via the I2C bus to measure ambient room light levels directly in Lux.
* **Power Supply:** The system runs on an external 5V/2A power supply to manage the high current demands of the LED, while the ESP32 regulates down to 3.3V logic.

---

### 2. Modulation and Demodulation

Data transport is executed directly across an optical channel using basic binary mapping over a dedicated hardware serial connection.

* **Modulation Scheme:** The system utilizes On-Off Keying (OOK) as its modulation framework. Digital signals are mapped directly to light intensity: the LED is driven fully ON to represent a logic '1' and fully OFF to represent a logic '0'.
* **Demodulation & Timing:** The receiving station captures light pulses via the photodiode circuit. The incoming signals are fed into a digital input pin on the ESP32, which relies on hardware interrupts to sample and decode data bits cleanly.
* **Line Rates:** The physical layer over light operates at a baseline line rate of 19,200 baud (`Serial2`), while communication to the host computer is handled at 115,200 baud (`Serial`).

---

### 3. Adaptive Speed Control

To maintain channel availability under fluctuating noise floors, the system continuously evaluates the optical medium and updates timing dynamics dynamically.

* **Mathematical Modeling:** The firmware evaluates link quality by calculating the Signal-to-Noise Ratio ($SNR$) based on the current Lux reading from the BH1750 sensor:

$$SNR = 20 \cdot \log_{10}\left(\frac{\text{Signal}}{\text{Noise Floor}}\right)$$


* **Dynamic Operational Modes:** Based on real-time Lux levels, the system alters the character transmission delay (`txDelay`) variable passed to the modulation function:
1. **Turbo Mode:** Triggered in low-light environments ($\text{Lux} \le 200$). The noise floor is low, allowing the bit delay to drop to its minimum value for maximum line throughput.
2. **Balanced Mode:** Triggered under moderate ambient light ($200 < \text{Lux} \le 1000$), introducing a moderate character pacing ($500\ \mu\text{s}$) to stabilize data delivery.
3. **Robust Mode:** Triggered in highly illuminated environments ($\text{Lux} > 1000$). The system increases the transmission delay (up to $2000\ \mu\text{s}$), giving the receiver more window time to resolve signals from the noise floor.



---

### 4. Encryption and Security Protocol

Data privacy is protected on the edge using a selective, low-overhead cryptographic pipeline.

* **Cryptographic Libraries:** The system deploys AES-128 encryption by leveraging the `mbedTLS` library, which directly interfaces with the ESP32’s built-in hardware crypto-accelerator.
* **Hybrid Security Strategy:** To avoid overwhelming the processor with large bulk streams, the system monitors packet lengths before transmission:
1. **Short Packets (< 64 Bytes):** Identified as critical commands or passwords, these packets are encrypted using AES-128 in Electronic Codebook (ECB) mode. ECB mode is selected because it is stateless; if a packet is lost due to a physical path blockage, the link self-heals immediately without losing synchronization.
2. **Long Packets (> 64 Bytes):** Flagged as bulk streams, these bypass encryption completely to maintain raw transmission speed.


* **Padding & Framing:** The firmware uses a custom calculation formula ($((len + 1 + 15) / 16) * 16$) to append null bytes and achieve the required 16-byte AES block alignment. Encrypted data frames are tagged with an `"ENC:"` protocol header so the receiver recognizes and decrypts them.

---

### 5. Error Detection and Correction

To withstand optical channel degradation caused by physical distance, alignment angles, or ambient jitter, the system embeds localized recovery logic.

* **Forward Error Correction (FEC):** Instead of requesting high-latency data retransmissions, the system adds data redundancy directly into the transmitted message payload framework.
* **Packet Frame Structure:** Data is packed into a dedicated validation string: `<Message | Checksum | Message Copy>`.
* **Algorithmic Verification Tree:** 1.  The receiver splits the intercepted frame and computes the ASCII sum checksum of the primary message.
2.  If a bit-flip is detected due to channel noise, the checksum fails. The receiver's state machine moves to the backup copy.
3.  If the backup copy evaluates correctly against the checksum, the firmware resolves the error on-the-fly (`[FEC FIXED]`), passing clear text to the analytics console without dropping the frame.
4.  If both copies fail validation, the system logs a packet failure to keep an accurate record of the current Bit Error Rate (BER).