# Fusion2Sphere

Takes two raw GoPro Fusion frames (for front and back camera) and converts them to a single equirectangular projection.

[A full description of the scripts logic can be seen here](http://paulbourke.net/dome/dualfish2sphere/).

[Fusion2Sphere is implemented in our GoPro Frame Maker script which is probably better suited for your requirements](https://github.com/trek-view/gopro-frame-maker/).

## Installation

The fusion2sphere command line utility should build out of the box on Linux using the simple Makefile provided. The only external dependency is the standard jpeg library (libjpeg), the lib and include directories need to be on the gcc build path. The same applies to MacOS except Xcode and command line tools need to be installed.

```
$ git clone https://github.com/trek-view/fusion2sphere
$ make -f Makefile
$ @SYSTEM_PATH/fusion2sphere
```

## Usage

### Preparation

This script is designed to be used with frames.

If using video mode (not photos) you will first need to convert both `.mp4` videos to frames and then pass the two corresponding frames to the script.

You can use ffmpeg to split your `mp4` videos into frames (below at a rate of 1 FPS).

```
$ ffmpeg -i FRVIDEO1.mp4 -r 1 -q:v 1 FR/img%d.jpg 
$ ffmpeg -i BKVIDEO2.mp4 -r 1 -q:v 1 BK/img%d.jpg 
```

### Lighting issues

This script does not normalise for different lighting levels (apeture settings) on each lens.

You can see an example of the light/dark effect this leads to in our examples.

![](testframes/18mp/single/STITCHED/G075169.jpg)

Therefore, you might want to proprocess frames to normalise lighting levels between front/back images.

### Script

```shell
$ fusion2sphere [options] FRONT BACK PARAM.txt
```

Variables:

* `FRONT`: path to single front image / path to directory of front images
* `BACK`: path to single back image / path to directory of back images
* `PARAM.txt` correct parameter text file for image size (mode used on Fusion)
	* 18mp = `parameter-examples/photo-mode.txt`
	* 5.2k = `parameter-examples/video-5_2k-mode.txt`
	* 3k = `parameter-examples/video-3k-mode.txt`

Options:

* `-w` n: sets the output image size, default: 4096
* `-a` n: sets antialiasing level, default: 2
* `-b` n: longitude width for blending, default: no blending
* `-q` n: blend power, default: linear
* `-e` n: optimise over n random steps
* `-p` n n n: range search aperture, center and rotations, default: 10 20 5
* `-f` flag needs two images one from front and second from back.
* `-o` flag outputs the final image.
* `-d`: debug mode

#### Examples (MacOS)

##### Use a GoPro Fusion 18mp photo(s)

**Single image**

```shell
$ /Users/dgreenwood/fusion2sphere/fusion2sphere -b 5 -w 5760 -f testframes/18mp/single/FR/GF075169.JPG testframes/18mp/single/BK/GB075169.JPG -o testframes/18mp/single/STITCHED/G075169.jpg parameter-examples/photo-mode.txt
```

**Directory of images**

_Note: directories image names will be sorted in ascending order for pairing._

```shell
$ /Users/dgreenwood/fusion2sphere/fusion2sphere -b 5 -w 5760 -f testframes/18mp/directory/FR/%06d.jpg testframes/18mp/directory/BK/%06d.jpg -o testframes/18mp/directory/STITCHED/%06d parameter-examples/photo-mode.txt
```

##### Use a GoPro Fusion 5.2k video frame(s)

**Single image**

```shell
$ /Users/dgreenwood/fusion2sphere/fusion2sphere -b 5 -w 5200 -f testframes/5_2k/single/FR/GPFR7152_img1.jpg testframes/5_2k/single/BK/GPBK7152_img1.jpg -o testframes/5_2k/single/STITCHED/GP7152.jpg parameter-examples/video-5_2k-mode.txt
```

**Directory of images**

_Note: directories image names will be sorted in ascending order for pairing._

```shell
$ /Users/dgreenwood/fusion2sphere/fusion2sphere -b 5 -w 5200 -f testframes/5_2k/directory/FR/%06d.jpg testframes/5_2k/directory/BK/%06d.jpg -o testframes/5_2k/directory/STITCHED/%06d.jpg parameter-examples/video-5_2k-mode.txt
```

##### Use a GoPro Fusion 3k video frame(s)

**Single image**

```shell
$ /Users/dgreenwood/fusion2sphere/fusion2sphere -b 5 -w 3000 -f testframes/3k/single/FR/GPFR0003_img1.jpg testframes/3k/single/BK/GPBK0003_img1.jpg -o testframes/3k/single/STITCHED/GP0003.jpg parameter-examples/video-3k-mode.txt
```

**Directory of images**

```shell
$ /Users/dgreenwood/fusion2sphere/fusion2sphere -b 5 -w 3000 -f testframes/3k/directory/FR/%06d.jpg testframes/3k/directory/BK/%06d.jpg -o testframes/3k/directory/STITCHED/%06d.jpg parameter-examples/video-3k-mode.txt
```

### Metadata

Note, the resulting image frames will not have any metadata -- this is not covered by the script.

[Our GoPro Frame Maker script can do this](https://github.com/trek-view/gopro-frame-maker/) ([the logic for which is described here](https://github.com/trek-view/gopro-frame-maker/docs/LOGIC.md))

## Support

Join our Discord community and get in direct contact with the Trek View team, and the wider Trek View community.

[Join the Trek View Discord server](https://discord.gg/ZVk7h9hCfw).