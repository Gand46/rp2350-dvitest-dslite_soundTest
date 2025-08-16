# Nintendo DS Lite DVI/HDMI with RP2350

This project captures raw display data from the Nintendo DS Lite flex connector and outputs a DVI (HDMI-compatible) video stream.

The video data is upscaled with a 2x nearest neighbor scaling and outputs a 640x480@60hz DVI signal.

The firmware runs on a RP2350.

It is currently in a ugly state and needs to be cleaned up - but it works. It has been tested on the TOP screen connector of a Nintendo DS Lite.

My ambition was to support both Top and Bottom screens in the same vide output, but it requires some planning with regards to the pinout.


## Building

Ensure you have a properly installed [pico-sdk](https://github.com/raspberrypi/pico-sdk) and related tools.

```
$ cmake -S . -B build -G Ninja -DPICO_PLATFORM=rp2350
$ cmake --build build
```

## Pinout

Simplified pinout for the RP2350:

```
0-5   = B
6-11  = G
20-25 = R
26    = DCLK - Pixel clock
27    = GSP  - Start of frame - goes low just before top left pixels start
28    = SPL  - Start of row   - goes high and low 2 clk before active pixels start
```

### Pinout of relevant pins

| Function | Explanation | TOP pin | BOTTOM pin |
| - | - | - | - |
| SPL | Start Pulse? Active pixels after 2 CLK | 30 | 30 |
| GSP | Global Start Pulse? Start of frame | 7 | 7 |
| DCLK | Pixel Clock | 29 | 29 |
| TOP\_LDR0 | TOP Red bit 0 | 22 |  |
| TOP\_LDR1 | TOP Red bit 1 | 23 |  |
| TOP\_LDR2 | TOP Red bit 2 | 24 |  |
| TOP\_LDR3 | TOP Red bit 3 | 25 |  |
| TOP\_LDR4 | TOP Red bit 4 | 26 |  |
| TOP\_LDR5 | TOP Red bit 5 | 27 |  |
| TOP\_LDG0 | TOP Green bit 0 | 15 |  |
| TOP\_LDG1 | TOP Green bit 1 | 16 |  |
| TOP\_LDG2 | TOP Green bit 2 | 17 |  |
| TOP\_LDG3 | TOP Green bit 3 | 19 |  |
| TOP\_LDG4 | TOP Green bit 4 | 20 |  |
| TOP\_LDG5 | TOP Green bit 5 | 21 |  |
| TOP\_LDB0 | TOP Blue bit 0 | 9 |  |
| TOP\_LDB1 | TOP Blue bit 1 | 10 |  |
| TOP\_LDB2 | TOP Blue bit 2 | 11 |  |
| TOP\_LDB3 | TOP Blue bit 3 | 12 |  |
| TOP\_LDB4 | TOP Blue bit 4 | 13 |  |
| TOP\_LDB5 | TOP Blue bit 5 | 14 |  |
| TOP\_SPLO | Speaker Left Out | 44 |  |
| TOP\_SPRO | Speaker Right Out | 40 |  |
| BOTTOM\_LDB0 | BOTTOM Blue bit 0 |  | 22 |
| BOTTOM\_LDB1 | BOTTOM Blue bit 1 |  | 23 |
| BOTTOM\_LDB2 | BOTTOM Blue bit 2 |  | 24 |
| BOTTOM\_LDB3 | BOTTOM Blue bit 3 |  | 25 |
| BOTTOM\_LDB4 | BOTTOM Blue bit 4 |  | 26 |
| BOTTOM\_LDB5 | BOTTOM Blue bit 5 |  | 27 |
| BOTTOM\_LDG0 | BOTTOM Green bit 0 |  | 15 |
| BOTTOM\_LDG1 | BOTTOM Green bit 1 |  | 16 |
| BOTTOM\_LDG2 | BOTTOM Green bit 2 |  | 17 |
| BOTTOM\_LDG3 | BOTTOM Green bit 3 |  | 19 |
| BOTTOM\_LDG4 | BOTTOM Green bit 4 |  | 20 |
| BOTTOM\_LDG5 | BOTTOM Green bit 5 |  | 21 |
| BOTTOM\_LDR0 | BOTTOM Red bit 0 |  | 9 |
| BOTTOM\_LDR1 | BOTTOM Red bit 1 |  | 10 |
| BOTTOM\_LDR2 | BOTTOM Red bit 2 |  | 11 |
| BOTTOM\_LDR3 | BOTTOM Red bit 3 |  | 12 |
| BOTTOM\_LDR4 | BOTTOM Red bit 4 |  | 13 |
| BOTTOM\_LDR5 | BOTTOM Red bit 5 |  | 14 |

