# Tiny Arkanoid

Daniel Champagne's Tinyjoypad game, adapted from the user-supplied 2025
ATtiny85 release. The original title screen, sprites and five level layouts
are stored as native SSD1306 page bytes. The game draws directly into the
MiaoUI framebuffer and uses the existing OLED DMA and Beep Service.

Turn the board 90° counterclockwise as for Pac-Man. The board's UP and DOWN
buttons become the paddle's left and right. Tap CONF to start, and tap again
to serve. Hold CONF to pause. A long hold returns to the Game menu. Re-entry resumes the retained ball,
paddle, bricks, lives and level in RAM.

Ball position and velocity use hundredths of a pixel. The fixed-step clock
discards elapsed time on STOP1 wake. Short PWM cues use the original
delay-loop tone parameters, including the first two notes of the title cue.

[Source project](https://github.com/phoenixbozo/TinyJoypad) ·
[GPL v3](LICENSE-GPL-3.0)
