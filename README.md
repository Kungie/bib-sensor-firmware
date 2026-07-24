# bib. — BLE-based Occupancy Sensor Firmware

This repository contains the firmware for the BLE-based sensor node used in **bib.**, a privacy-friendly occupancy estimation system for study rooms at the University Library (Universitätsbibliothek) of RWTH Aachen University.

The sensor passively scans for Bluetooth Low Energy (BLE) advertising packets to estimate room occupancy — without reading, storing, or transmitting any device identifiers (MAC address, device name) or other personally identifiable information. Only the number and signal strength (RSSI) of received packets are evaluated on-device to compute an anonymized occupancy signal.

This firmware is one component of a larger system that also includes a Flask-based backend, a machine learning prediction pipeline, and a web frontend. Those components are not part of this repository.

## How It Works

- The sensor performs periodic BLE scans (5 sub-scans of 3 seconds each per measurement cycle).
- Received advertising packets are filtered by an RSSI threshold, calibrated to approximate the physical boundaries of the target room.
- Packets passing the filter are counted and averaged over multiple scan cycles.
- The resulting aggregate value is transmitted via WiFi to a backend API — no raw packet data, device identifiers, or per-device history ever leaves the sensor.

## Hardware

- ESP32-WROOM-32 (tested on Elegoo ESP32 development boards)
- Powered via USB / power bank during the pilot phase; PoE-capable boards are being evaluated for permanent deployment

## Setup

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) and the ESP32 board package.
2. Install the required libraries: `WiFi.h`, `WiFiClient.h`, `NimBLEDevice.h`, `esp_task_wdt.h`.
3. Copy `config.h.example` to `config.h` and fill in your WiFi credentials and backend endpoint:
```cpp
   cp config.h.example config.h
```
4. Adjust sensor parameters (RSSI threshold, scan timing, sensor ID) as needed for your environment — see comments in `sniffer.ino`.
5. Flash the firmware to your ESP32 board.

## Privacy by Design

This firmware was built around the principle that anonymization should happen as early as possible — on the sensor itself, before any data leaves the device:

- No MAC address or device name is ever read from received packets.
- The BLE library's internal duplicate filter is cleared after every sub-scan (`clearResults()`), so no per-device history is retained even transiently.
- Only an aggregated, rounded measurement is transmitted per cycle — individual devices or persons cannot be reconstructed from this value.

## Background

This project was developed as part of a student research assistant (HiWi) position at the IT department of the RWTH Aachen University Library. A detailed description of the full system — including the backend architecture, calibration process, and the machine learning model used to estimate seat availability — is available in the accompanying paper:

> Kahraman, E., Jüptner, P., Schmitz, D., Keilholz, C. (2026). *bib. – Datenschutzfreundliche Erfassung der Lernraumauslastung per BLE-Sensorik an der UB der RWTH Aachen.* 

## License

This project is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0) — see [LICENSE](LICENSE) for details.

## Disclaimer

This is a research/pilot project developed in an academic context. It is provided as-is, without warranty of any kind.
