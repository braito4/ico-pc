## New

- The game's version is shown in the top-right corner of the Options menu's first page, and at the top of the log, so a problem report can say which version it came from.
- On Windows, Linux and Mac, if the graphics driver crashes while the game gets its graphics ready, the game starts itself again, trying a gentler way each time (on Windows it can switch to Direct3D 12).
- The log says which graphics driver version you have and what the game was doing when it crashed.
- The log says more about stutters, so a report about uneven motion can be looked into.
- Developer documentation: how the port works, part by part, and a list of every way it differs from the PS2 game, under [docs/port](https://github.com/nathanialf/ico-pc/blob/main/docs/port/README.md).
- The player guides cover every platform's install and file locations (Windows, Linux, Steam Deck, Mac, iPhone, Android).

## Fixed

- Linux: the computer no longer goes to sleep or locks the screen while you play with a controller.
- With Vertical sync on and the frame-rate limit equal to your screen's refresh rate, turning the camera no longer shows an occasional repeated picture.
- Mac: a save folder or second memory card set in `ico-pc.ini` is used.
- Linux, Steam Deck and Mac: when the game cannot start, for example with the wrong disc image, it shows a message that says why instead of just closing.
