# Tetris v3

Adapted from Daniel C's 2025 Tinyjoypad ATtiny85 source archive. The port
uses the original 12×19 well, seven pieces, scoring and 5×5 rotation pivot.
The 64×128 R1 portrait view uses 4×4 blocks on a 5px pitch with a one-pixel
gap at both side walls. A dashed line at y=10 marks the bottom of the
top-out zone (row 1, zero-based). The title, HUD and NEXT preview use
u8g2 primitives; the OLED buffer and four sound cues are unchanged.

Turn the board 90° counterclockwise as for Pac-Man. Portrait LEFT/RIGHT
move, DOWN speeds up the fall, UP rotates counterclockwise and short CONF
starts or rotates clockwise. Hold CONF to pause and keep holding to exit.
Exiting retains the current board, piece, score and timers in RAM; re-entry
resumes without advancing off-screen time. The best score and saved game
are lost on MCU reset.

[Source project](https://github.com/phoenixbozo/TinyJoypad) ·
[GPL v3](LICENSE-GPL-3.0)
