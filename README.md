# M5 Chess Engine

Text-first chess search firmware for M5Stack. It does not draw a board. It accepts FEN positions and UCI-style moves over USB serial, searches legal chess moves on-device, and shows the best move on the M5 screen.

## Target Devices

- M5Stack Core
- M5Stack Core2

The source uses `M5Unified`, so nearby M5Stack ESP32 devices may work after adding a matching PlatformIO environment and M5Burner device category.

## Build

Install PlatformIO, then run one of these from this folder:

```powershell
pio run -e m5stack-core-esp32
python scripts\package_m5burner.py m5stack-core-esp32
```

For Core2:

```powershell
pio run -e m5stack-core2
python scripts\package_m5burner.py m5stack-core2
```

The packager creates two M5Burner-friendly outputs:

- `firmware/<env>/`: split flash files named with their burn addresses, matching the GitHub-style M5Burner package format.
- `exports/m5_chess_engine_<env>_merged.bin`: a single merged firmware image for M5Burner `USER CUSTOM` publishing, when PlatformIO's `esptool.py` is available.

## M5Burner

For a personal M5Burner upload:

1. Open M5Burner and sign in.
2. Go to `USER CUSTOM` > `Publish`.
3. Use the details from `m5burner.json`.
4. Use `exports/m5_chess_engine_<env>_merged.bin` as the firmware file.
5. Use `cover.png` as the cover image.

For a repository-style M5Burner package, keep `m5burner.json` beside the `firmware` folder. The firmware files inside each category folder use names like `m5_chess_engine_0x10000.bin`, where the suffix is the flash address.

## Device Controls

- `A`: cycle built-in positions
- `B`: search for the best move
- `C`: change search depth

USB serial runs at `115200`. Commands:

```text
help
new
fen <full FEN>
go depth 3
depth 4
move e2e4
moves
state
```

Depth is capped at 5 to keep the device responsive.
