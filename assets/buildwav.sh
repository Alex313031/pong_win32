#!/bin/bash

export HERE=${PWD} &&
export TARGET=../src/res &&

python3 ${HERE}/make_sine_wav.py &&

ffmpeg -y -i ${HERE}/pinball_fantasies_partyland.wav -filter:a "volume=0.50" -c:a adpcm_ms -ar 44100 -ac 2 ${TARGET}/music.wav &&

ffmpeg -y -i ${HERE}/racket.wav -filter:a "volume=0.60" -c:a adpcm_ms -ar 44100 -ac 2 ${TARGET}/racket.wav &&

ffmpeg -y -i ${HERE}/wall.wav -filter:a "volume=0.60" -c:a adpcm_ms -ar 44100 -ac 2 ${TARGET}/wall.wav &&

rm -fv ${HERE}/racket.wav &&
rm -fv ${HERE}/wall.wav &&

exit 0
