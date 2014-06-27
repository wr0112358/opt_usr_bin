#!/bin/bash

# get window id
window_id=$(xprop | grep 'window id' | cut -d\: -f 2)
# fill variables: WINDOW, X, Y, WIDTH, HEIGHT, SCREEN
eval $(xdotool getwindowgeometry --shell $window_id)

# urxtv256c with default decoration, font Inconsolata 8 has
# 576x1052 size for a 80x80 sized terminal
xdotool windowsize $WINDOW 576 1052
