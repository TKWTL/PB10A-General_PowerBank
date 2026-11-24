# Tiny Tris v3

Adapted from Daniel C's 2025 Tinyjoypad ATtiny85 source archive. The port
uses the original 12×19 well, seven piece matrices, score table, start
screen, sprites and next-piece preview. It draws into the existing OLED
framebuffer. Timed game updates and the line-clear flash run cooperatively.

Play in the board's normal landscape orientation. LEFT and RIGHT move the
piece, DOWN speeds up the fall and a short CONF release starts the game or
rotates the piece. Hold CONF to pause and keep holding to exit. The best
score is retained in RAM until the device resets.

[Source project](https://github.com/phoenixbozo/TinyJoypad) ·
[GPL v3](LICENSE-GPL-3.0)
