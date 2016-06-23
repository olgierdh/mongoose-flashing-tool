# Mongoose Flashing Tool

Mongoose Flashing Tool (also called MFT) is the
[Mongoose IoT Platform](https://github.com/cesanta/mongoose-iot)
flashing tool.
It is designed to be a simple wizard tool for flashing Mongoose Firmware
onto the target board and connect it with the
[Mongoose Cloud](https://mongoose-iot.com).
Also, MFT provides advanced options via command-line flags.

## EP8266

Flashing ESP8266 repeatedly is easier with additional wiring.
See [here](https://github.com/cesanta/mongoose-iot/blob/master/fw/platforms/esp8266/flashing.md)
for more details.

## CC3200

Flashing CC3200 requires `SOP2` jumper to be closed.
To make repeated flashing easier, consider installing an additional jumper wire
described [here](http://energia.nu/cc3200guide/).

## Firmware format

MFT expects firmware bundles as ZIP files.
ZIP archive must contain a file called `manifest.json`, which descibes firmware
contents.

Example (ESP8266):

```json
{
  "build_id": "20160621-141640/esp@f0f6c337+",
  "build_timestamp": "2016-06-21T14:16:40.207376",
  "name": "mongoose-iot",
  "parts": {
    "boot": {
      "addr": 0,
      "cs_sha1": "6a71a376c058d6c5a9ff7bf6e3279b24e2d59a8e",
      "src": "0x00000.bin"
    },
    "boot_cfg": {
      "addr": 4096,
      "cs_sha1": "e0c66649d1434eca3435033a32634cb90cef0f31",
      "src": "0x01000.bin"
    },
    "fs": {
      "addr": 901120,
      "cs_sha1": "c133f87fa4fb94b4a89051f7c83f9b5294ee927c",
      "fs_block_size": 4096,
      "fs_erase_size": 4096,
      "fs_page_size": 256,
      "fs_size": 131072,
      "src": "0xdc000.bin"
    },
    "fw": {
      "addr": 69632,
      "cs_sha1": "d8cca8cc270dbb42ef774a1344ab9ef21c33e11c",
      "src": "0x11000.bin"
    }
  },
  "platform": "esp8266",
  "version": "20160621141640"
}
```

A [script](https://github.com/cesanta/mft/blob/master/common/tools/fw_meta.py)
can be used to generate the manifest.

# Contributions

To submit contributions, sign
[Cesanta CLA](https://docs.cesanta.com/contributors_la.shtml)
and send GitHub pull request. You retain the copyright on your contributions.
