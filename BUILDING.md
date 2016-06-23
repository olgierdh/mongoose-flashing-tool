# Building MFT

MFT requires:

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

# Building static binaries

Before building MFT, you'll need to build static Qt libraries from source.

## Building static Qt

### Debian/Ubuntu

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
$ qmake && nice make -j 4
```

### Windows/MinGW

 * Make sure `Z:/opt` is mounted in the VM, output will go to `Z:/opt/qt5` and this is the Qt prefix build requires.
 * Follow the instructions for installing MinGW-64 MSYS2 [here](https://wiki.qt.io/MinGW-64-bit). Don't worry about the 64-bit part, it also installs 32-bit tools.
   * Remove unnecessary stuff:
     * `pacman -R -dd mingw-w64-i686-tcl mingw-w64-x86_64-tcl mingw-w64-i686-tk mingw-w64-x86_64-tk mingw-w64-i686-gcc-ada mingw-w64-x86_64-gcc-ada`
 * Add `export PATH="/c/msys2/mingw32/bin:$PATH"` to `~/.bashrc`
 * Download OpenSSL and Qt distros to `c:\qt5-win` and untar
 * Build OpenSSL first
   * Edit `crypto/rand/rand_win.c` and disable the `readscreen` function, otherwise build will fail with undefined references (`_imp__GetDeviceCaps@8', etc).
   * Use the following line to configure: `./Configure --prefix=/c/qt5-win/openssl no-asm no-idea no-mdc2 no-rc5 mingw64`
   * `make depend && make && make install`
 * Build and install Qt
   * `./configure -platform win32-g++ -prefix z:/opt/qt5 -I /c/qt5-win/openssl/include -L /c/qt5-win/openssl/lib -make libs tools -static -opensource -confirm-license -skip qt3d -skip qtcanvas3d -skip qtdoc -skip qtlocation -skip qtscript -skip qtmultimedia -skip qtsensors -skip qtwebengine -skip qtwebkit -skip qtwebkit-examples -openssl-linked`
   * `mingw32-make -j 4`
   * `mingw32-make install`
 * Bundle MinGW32 together with Qt (ugly, but works):
   * `cp -Rv /mingw32/bin z:/opt/qt5`
   * `cp -Rv /mingw32/lib/gcc /z/opt/qt5/lib`
   * `cp -Rv /mingw32/i686-w64-mingw32/lib z:/opt/qt5`
   * `cp -Rv /mingw32/include /mingw32/i686-w64-mingw32/include z:/opt/qt5`
   * `cp -Rv /c/qt5-win/openssl/lib z:/opt/qt5`

## Building a release

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

