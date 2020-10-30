# turnbinds

Provides turnbinds (`+left/+right`) with customizable yawspeed for CS:GO (useful in surf servers).

The commands `+left/+right` turn your view angles. A command which controls the turning speed (`cl_yawspeed`) exists in previous games in the series (1.6, Source), but was removed in CS:GO.

Use at your own risk!

## Usage

[Download](https://github.com/t5mat/turnbinds/releases/latest/download/turnbinds.exe) and run `turnbinds.exe`.

### Controls

- ESC/CTRL+C - quit
- Up/down arrows (+SHIFT) - change speed
- Left/right arrows (+SHIFT) - change rate
- R - reconfigure binds

## Notes

- The program simulates mouse input (no hooking of game client)
- Speed is measured in "mickeys" per second. [More information on MSDN](https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-mouse_event)
- The program is active whenever your mouse cursor is hidden, meaning it should work in any game (although it was built for CS:GO surf)
- Rate = maximum rate of mouse movement (smoothness of the turn). Default value should be good as long as in-game FPS isn't drastically reduced

## Building

Run `./build` on a Linux machine with Docker installed.
