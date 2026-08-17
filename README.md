# pico-dev-kit

A Raspberry Pi Pico based multi-interface development kit with USB high-speed
transport, interactive console, data recording, fault injection, and interface
monitoring.

## Features

| Feature | Description |
|---|---|
| **USB Console** | CDC ACM port 0 – interactive text commands |
| **USB Data Stream** | CDC ACM port 1 – binary frame stream to host |
| **I2C master/slave** | Configurable speed; slave driven by hardware IRQ |
| **SPI master/slave** | Configurable speed, CPOL, CPHA; slave via DMA |
| **Frame recorder** | Captures all traffic and streams it over USB data port |
| **Fault injection** | Send arbitrary byte sequences on any interface |
| **Interface monitor** | Periodic health checks; logs errors to console |

## Console commands

```
help
version
status
config i2c  master|slave <speed_kHz>
config spi  master|slave <speed_kHz> [cpol] [cpha]
record start|stop
inject i2c <addr_hex> <byte0> [byte1 …]
inject spi  <byte0> [byte1 …]
monitor start|stop
```

## USB data-port frame format

Each captured frame is sent as a binary packet on CDC port 1:

```
[4]  magic       0x50 0x44 0x4B 0x46  ("PDKF")
[4]  timestamp   microseconds since boot (little-endian uint32)
[1]  iface       1=I2C  2=SPI
[1]  role        0=master  1=slave
[2]  length      payload byte count (little-endian uint16)
[N]  data        captured bytes
```

## Building

Requirements: [pico-sdk](https://github.com/raspberrypi/pico-sdk) ≥ 1.5.0

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Flash `pico_dev_kit.uf2` to the Pico (hold BOOTSEL while plugging in).

## Pin assignments

| Signal | GPIO | Notes |
|---|---|---|
| SPI SCK | 2 | |
| SPI TX  | 3 | |
| SPI RX  | 0 | |
| SPI CS  | 1 | Active-low (master); driven by master (slave) |
| I2C SDA | 4 | Pull-up required |
| I2C SCL | 5 | Pull-up required |

Pins can be overridden by defining `SPI_SCK_PIN`, `SPI_TX_PIN`, etc. in
`CMakeLists.txt` via `target_compile_definitions`.

## License

See [LICENSE](LICENSE).