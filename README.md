# Description
This is pure C GStreamer-based demo application showing how to use videomixer and optionally stream output to Twitch.
(c) Alexander Voitenko

# Build requirements
- Any recent C compiler
```bash
$ sudo apt install build-essential
```
- GStreamer development packages
```bash
$ sudo apt install apt-get install libgstreamer1.0-0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-doc gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
```
- CMake
```bash
$ sudo apt install cmake
```

# Build
```bash
$ mkdir build && cd build
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
Note: absolute paths are also supported.
In both cases windows with mixed video stream will be created.

# Showcase
![Windowed app](/doc/images/window_screenshot.png?raw=true "Windowed app")
![Twitch example](/doc/images/twitch_screenshot.png?raw=true "Twitch example")
