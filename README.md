# `dp2`

This is the code for the (failed) design project 2 for my
engineering class that I worked on with some other dudes.

Shoutout to Joseph, Andrew and Anthony.

# Game

The game roughly works by having a box with 4 RGB sensors
and 2 decks of red, green and blue Uno cards. You select 2
cards each round indicated by a beep and match the cards
to whichever corner of the box has the same color RGB LED
lit.

# Implementation

The game is implemented using a state-machine design with 3
states for pre-round, waiting and transition states between
each round. The colors on each card are determined by
taking the greatest color intensity from the RGB sensor
accounting for ambient color intensity as well as filtering
out insufficiently intense colors. 5 color samples of the
same color must be taken in order to accept it as the
desired color for that slot.

# Notes/Caveats

- The game doesn't actually work and there could be a
few reasons that I suspect may be the case
  - Start button seemed to be shorted for some reason
  - Ambient light might have overcompensated and dampened
  the color intensity filter too much
  - Adding statistical analysis made the results
  significantly worse so it is possible that either the RGB
  sensors didn't even work in the first place or that the
  colors were being incorrectly interpreted
  - Color intensity filtering by an absolute constant seems
  to be really inconsistent and basically results in the
  game being overly sensitive or overly insensitive to the
  cards being held up to the sensor
- Probably a better solution to naive statistical analysis
would have been machine learning (heh)
- Pulled an all-nighter in order to get everything to
"randomly working" state, perhaps our time should have been
managed more effectively

# Credits

Built with [Arduino](https://www.arduino.cc/)

