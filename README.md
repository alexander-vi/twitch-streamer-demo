# Description
This is pure C GStreamer-based demo application showing how to use videomixer element and optionally stream the output to Twitch.

# Supported platforms
- Linux

With minor modifications source code should work on other platforms.

# Build requirements
- Any recent C compiler
```bash
$ sudo apt install build-essential
```
- GStreamer development packages
```bash
$ sudo apt install libgstreamer1.0-0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-doc gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
```
- CMake
```bash
$ sudo apt install cmake
```

# Build
```bash
$ mkdir -p build && cd build
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ make
$ cd ..
```

# Run
This demo supports 2 mode of operations:
1. With Twitch streaming
```bash
$ ./build/twitch-streamer live_111111111_aaaabbbcccddddeeeeffffggghhhhh ./data/sintel_trailer-480p.webm ./data/big_buck_bunny_trailer-360p.mp4 ./data/the_daily_dweebs-720p.mp4
```
2. Without streaming
```bash
$ ./build/twitch-streamer ./data/sintel_trailer-480p.webm ./data/big_buck_bunny_trailer-360p.mp4 ./data/the_daily_dweebs-720p.mp4
```
In both cases windows with mixed video stream will be created.

> **_NOTE:_**  absolute paths are also supported.

# Known limitations

> :warning: Twitch stream does not start immediately. ~20 seconds is required to see it on [twitch.tv](https://twitch.tv/).

- Only 3 output videos composition is supported. To change this, source code modifications will be required. Though it should not be very difficult.
- Output video dimensions are hardcoded as constants in sources.
- Audio is taken from the first video. This is also a constant in the code.

# Showcase
Windowed app:
![Windowed app](/doc/images/window_screenshot.png?raw=true "Windowed app")

Twitch stream:
![Twitch example](/doc/images/twitch_screenshot.png?raw=true "Twitch example")

# License
MIT