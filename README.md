# Halcyon

**A Minimal, Low-Power Hi-Fi Walkman**

Halcyon is an open-source, wired-only, physical-button digital audio player in a Walkman form factor.

Inspired by the Persona 3 Walkman and its real-life equivalent, the Sony NW-S203F.
Halcyon is available on GitHub under the <a href="https://github.com/Shim06/Halcyon/blob/main/LICENSE" target="_blank">GNU General Public License v3.0 (GPLv3)</a>.

> **Status: early development.** Firmware is being prototyped on an STM32F411 Black Pill. The custom board and enclosure is not designed yet.

## Design Philosophy

Halcyon is designed to be a minamalist, low-power, high-quality digital audio player recreating the classic retro digital audio player design. No wireless, no touchscreen, no apps, no streaming. Your entire library lives on a microSD card. It is a pocketable, portable, and easy-to-use device operated with physical buttons and lasts for several hours on a single charge.

## Hardware

| Block | Part |
|---|---|
| MCU | STM32U575 |
| DAC + headphone amp | CS43131 |
| Power Supply | TPS63000 (3.3V) & TPS62A0569 + TPS7A20 (1.8V) |
| Display | SSD1306 0.91" 128×32 monochrome OLED |
| Storage | microSD, 4-bit SDIO, Fat32/ExFAT |
| USB | USB-C |
| PCB | 4-layer, Top (Signal) / GND / PWR / Bottom (Signal) |

## Firmware

The firmware is bare-metal C: interrupt-driven superloop. The MCU sleeps most of the time, waking up only to handle interrupts, saving power. The audio path is driven by DMA. The audio player will support WAV, FLAC, and MP3 playback.

### Playback pipeline

```
microSD --FatFs/SDIO--> decoder --> PCM buffer --DMA/SAI--> CS43131 --> headphones
        (WAV / FLAC / MP3)
```

The DMA runs in circular mode over a double buffer. When it finishes half the buffer, a HAL callback flags that half as empty. The superloop then refills it from the SD card.

### Library indexing

FatFs opens files by path, not by number. There is no "open track 4,217". Holding the whole library in RAM caps how big a library you can support. So the firmware scans once and writes index files (`track_indexer.c/h`) that map a plain integer to a file:

- Recursive scan writes a `.hidx` file per folder plus an aggregate `library.hidx`.
- Any entry is read by position with a seek. Only one entry is in RAM at a time.
- Sorting and filtering happen at browse time over the flat file. Records are never physically reordered.

Library size is bounded by the card and the scan time, not by RAM. Navigation is an integer plus prev/next, scoped to a folder or the whole library.

## Status

| Area | State |
|---|---|
| FatFs over SDIO | Prototype implemented on F411 |
| WAV DMA playback | Prototype implemented on F411 |
| File indexing | Implemented (Not tested) |
| Audio metadata parsing | Not implemented |
| FLAC / MP3 decode | Not implemented |
| PCB | Not started |

## Roadmap

1. Graphical User Interface and navigation
2. Audio metadata parsing
3. FLAC and MP3 decoding implementation
4. `AudioSource` abstraction (WAV/FLAC/MP3) once all decoders are implemented
5. Rewrite firmware to run on the STM32U575
6. Schematic and PCB design
7. Enclosure design
8. USB DAC mode

## Building

TODO: toolchain, build steps, and flashing instructions.

## Contributing

Pull requests are welcome. For major changes, please open an issue first
to discuss what you would like to change.

## License

This project is licensed under the GNU General Public License v3.0 (GPLv3) - see the [LICENSE](LICENSE) file for more details.
