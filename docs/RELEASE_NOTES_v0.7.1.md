## New

- The port's version is shown in the top-right corner of the Options menu's first page, and at the top of the log, so a report can say which build it came from.
- If the graphics driver crashes while the game gets its graphics ready, the game starts itself again on its own, trying a gentler way each time (on Windows it switches to Direct3D 12), instead of just closing. The log says which driver version you have and what the game was doing when it crashed.
- Developer documentation: how the port runs the PS2 game on a computer, part by part, and a list of every way it differs from the original, under [docs/port](https://github.com/nathanialf/ico-pc/blob/main/docs/port/README.md).
- The player guides cover every platform's install and file locations (Windows, Linux, Steam Deck, Mac, iPhone, Android), and are checked against the code.

## Fixed

- With Vertical sync on and the frame-rate limit equal to your screen's refresh, the occasional repeated picture while panning the camera is gone: on Windows the game now follows the screen's own timing instead of the refresh rate the system reports, and with Direct3D 12 it waits for the screen before drawing.
- Mac: the `saves=` and `saves2=` lines in `ico-pc.ini` are read from the file the game actually uses.
- The log's 10-second summary says which step of drawing was slowest, and prints a line for any picture that took longer than a screen refresh, so a stutter report can be read.
