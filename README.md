# ESP32 RR1 Timer

ESP-IDF firmware for an RR1 timer device built around the ESP32-C5. The firmware captures lane sensor transitions and GPS PPS timing, turns them into protobuf messages, publishes data over TLS MQTT, reports device health, supports Wi-Fi provisioning, and performs OTA firmware updates.

## What It Does

- Captures precise timing edges from GPS PPS, lane 1, and lane 2 inputs.
- Extends hardware capture ticks to 64-bit timestamps and correlates lane events with GPS time.
- Buffers lane transitions and periodically publishes protobuf payloads over MQTT.
- Reports runtime health including GPS PPS status, heap, CPU temperature, Wi-Fi RSSI, MQTT latency, uptime, build metadata, and publish credits.
- Provisions Wi-Fi using the `espressif/network_provisioning` manager (the ESP-IDF 6.0 successor to `wifi_provisioning`).
- Fetches MQTT credentials from a configured RR1 backend using HTTPS.
- Checks for firmware updates by comparing the OTA binary ETag with the value stored in NVS.
- Parses GPS NMEA sentences from UART for diagnostic GPS information.

## Main Runtime Flow

The application entry point is `main/umain.c`.

Startup sequence:

1. Initialize NVS, network interfaces, and the default event loop.
2. Start the capture task for lane sensors and GPS PPS.
3. Start Wi-Fi provisioning or connect using stored credentials.
4. Fetch MQTT host, certificate, and private key from the configured backend.
5. Start the MQTT client.
6. Initialize tap/I2C handling.
7. Start OTA update checking.
8. Start GPS NMEA parsing.
9. Continue logging the running firmware version.

## Project Layout

- `main/` - top-level application entry point and OTA update task.
- `components/rr1_capture/` - timing capture, lane history, MQTT publishing, health payloads, CPU idle tracking, and I2C/tap handling.
- `components/rr1_messages/` - generated protobuf-c bindings for timer data; schemas live in the peer `rr1-timer-protobufs` repo.
- `components/rr1_wifi_prov/` - Wi-Fi provisioning, NVS helpers, reset button handling, and stored RR1 configuration.
- `components/rr1_gps/` - UART GPS input and NMEA parsing.
- `components/rr1_pin_defs/` - board pin definitions.
- `components/rr1_blink/` - LED and laser status output.
- `tools/` - helper scripts for CI/AWS deployment.
- `.github/workflows/build-idf.yml` - CI build, format check, artifact upload, and optional S3 firmware publishing.

## Target And Build

- ESP-IDF version in CI: `v6.0.3`
- Target in CI/current config: `esp32c5`
- Default partitioning: two large OTA app partitions
- Flash size default: 4 MB
- MQTT transport: SSL/TLS
- OTA binary path: `/firmware/esp32-rr1-timer.bin` on the configured backend host

Typical local build:

```sh
idf.py set-target esp32c5
idf.py build
```

## Protobuf Bindings

The timer `.proto` schemas are maintained in the peer `rr1-timer-protobufs`
repository. Its GitHub Action generates protobuf-c bindings and publishes them
as the release asset `rr1-messages-generated.zip` (release assets, unlike
Actions artifacts, do not expire). This repository does not track those
generated `.pb-c.c` or `.pb-c.h` files; CI downloads them before formatting and
building.

The proto repository is public, so no token is needed:

```sh
./tools/download_rr1_messages_artifact.sh
```

By default this fetches GitHub's "latest" release of the proto repository,
which is moved to a new, verified release on every push to its `main` branch.
To pin a build to a tagged set of bindings, set `RR1_PROTO_TAG`:

```sh
RR1_PROTO_TAG=v1.0.0 ./tools/download_rr1_messages_artifact.sh
```

If you download the asset manually, update this firmware component with:

```sh
./tools/update_rr1_messages_from_artifact.sh <artifact-directory>
```

Typical flash and monitor:

```sh
idf.py -p <PORT> flash monitor
```

## Host Tests

Some regressions are covered by lightweight host tests that do not require an
ESP32 target or ESP-IDF environment. Run them with:

```sh
python3 -m unittest discover -s tests
```

## Firmware Publishing

The GitHub Actions workflow builds every branch. For selected branch names, `tools/aws_env.sh` sets an AWS account and S3 bucket, then CI uploads:

```text
build/esp32-rr1-timer.bin
```

to:

```text
s3://<bucket>/firmware/esp32-rr1-timer.bin
```

Devices use the configured RR1 host from NVS to check that same firmware path over HTTPS.
