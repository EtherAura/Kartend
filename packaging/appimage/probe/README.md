# kartend-appimage-probe

A test helper for the AppImage, not part of Kartend and not shipped. See the
comment at the top of `main.cpp` for what it checks and
`../smoke-test.sh` for how it is run.

## The two clips

`h264-aac.mp4` and `vp9-opus.webm` are one second of FFmpeg's built-in test
pattern (64x64, 10 frames) with a 440 Hz tone. They hold no third-party
material. The probe plays them through the bundled Qt Multimedia to prove
that the AppImage decodes the formats video previews usually come in, in
software, on a machine with no graphics hardware at all.

They were made with:

```bash
ffmpeg -f lavfi -i "testsrc2=size=64x64:rate=10:duration=1" \
       -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=1" \
       -c:v libx264 -profile:v baseline -pix_fmt yuv420p -g 5 -bf 0 -crf 30 \
       -c:a aac -b:a 24k -ac 1 -shortest -movflags +faststart \
       -map_metadata -1 -fflags +bitexact -flags:v +bitexact -flags:a +bitexact \
       h264-aac.mp4

ffmpeg -f lavfi -i "testsrc2=size=64x64:rate=10:duration=1" \
       -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" \
       -c:v libvpx-vp9 -pix_fmt yuv420p -g 5 -b:v 0 -crf 45 -deadline good -cpu-used 4 \
       -c:a libopus -b:a 16k -ac 1 -shortest -map_metadata -1 -fflags +bitexact \
       vp9-opus.webm
```
