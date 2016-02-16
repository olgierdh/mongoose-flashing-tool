# Flashnchips

Flashnchips is the Smart.js flashing tool. It is designed to be simple, but
provide advanced options via command-line flags.

## EP8266

Flashing ESP8266 repeatedly is easier with additional wiring.

See [here](https://github.com/cesanta/smart.js/blob/master/smartjs/platforms/esp8266/flashing.md)
for more details.

## Firmware format

Flashnchips expects firmware bundles as ZIP files.
ZIP archive must contain a file called `manifest.json`, which descibes firmware
contents.

Example (ESP8266):

```json
{
  "name": "smartjs",
  "platform": "esp8266",
  "version": "20160128095944",
  "build_id": "20160128-095944/fnc@043a9d7e.",
  "build_timestamp": "2016-01-28T09:59:44.439407",
  "parts": {
    "boot": {
      "addr": "0x0",
      "cs_sha1": "f975bb12df29c332bb7f44c7c6b842038da0f066",
      "src": "0x00000.bin"
    },
    "boot_cfg": {
      "addr": "0x1000",
      "cs_sha1": "e0c66649d1434eca3435033a32634cb90cef0f31",
      "src": "0x01000.bin"
    },
    "fs": {
      "addr": "0xe0000",
      "cs_sha1": "5216007432ee228dba8f3e300117d15953b21226",
      "src": "0xe0000.bin"
    },
    "fw": {
      "addr": "0x11000",
      "cs_sha1": "4d60976cc0953388a15c1ac597b0bab39b6d1404",
      "src": "0x11000.bin"
    }
  }
}
```

A [script](https://github.com/cesanta/fnc/blob/master/common/tools/fw_meta.py)
can be used to generate the manifest.

## Build

### Building

Flashnchips requires:

- Qt 5
- libftdi
- Python with GitPython module

Install the dependencies (Debian/Ubuntu):

```
$ sudo apt-get install build-essential qt5-qmake libqt5serialport5-dev libftdi-dev git python-git
```

Build a GUI version with:

```
$ QT_SELECT=5 qmake && make -j 3
```

Or build a CLI-only version with:

```
$ QT_SELECT=5 qmake -config cli && make -j 3
```

### Building static binaries

Before building F&C, you'll need to build static Qt libraries from source.

Install the dependencies and build Qt (Debian/Ubuntu):

```
$ sudo apt-get install build-essential git python-git wget libglib2.0-dev libudev-dev libftdi-dev libfontconfig1-dev libjpeg-dev libssl-dev libicu-dev libjpeg-dev
$ wget -c http://download.qt.io/official_releases/qt/5.5/5.5.1/single/qt-everywhere-opensource-src-5.5.1.tar.gz
$ tar xzf qt-everywhere-opensource-src-5.5.1.tar.gz
$ cd qt-everywhere-opensource-src-5.5.1
$ ./configure -make 'libs tools' -static -prefix /opt/qt5 -opensource -confirm-license -skip qt3d -skip qtcanvas3d -skip qtdoc -skip qtlocation -skip qtscript -skip qtmultimedia -skip qtsensors -skip qtwebengine -skip qtwebkit -skip qtwebkit-examples
$ nice make -j4
$ nice make -j4 install
```
Note: if you're only going to build CLI version, you can add `-no-gui` to Qt's `configure` line which will save quite a bit of time building.

Then build F&C Qt:

```
$ export PATH="$PATH:/opt/qt5/bin"
$ qmake && make -j 3
```

### Building a release

(Mostly of interest to Cesanta)

There is a special makefile and accompanying Docker images for building release binaries.
To build release binaries, you do not need to install any dependencies, they are all provided in the Docker images.
For Mac and Windows binaries you will need to get the code signing certificate.

```
$ make -f Makefile.release ubuntu32 ubuntu64 win
```

And, on a Mac machine:

```
$ make -f Makefile.release mac
```

# Contributions

People who have agreed to the
[Cesanta CLA](https://docs.cesanta.com/contributors_la.shtml)
can make contributions. Note that the CLA isn't a copyright
_assigment_ but rather a copyright _license_.
You retain the copyright on your contributions.
