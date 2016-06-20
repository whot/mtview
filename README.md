mtview - Multitouch Viewer
==========================

The mtview tool shows a graphical view of a kernel multitouch device. It
reads events directly off the kernel device and is thus unaffected by any
userspace processing.

This repository is a fork from the discontinued
http://bitmath.org/code/mtview/

How to build
------------

```
git clone https://github.com/whot/mtview
cd mtview
./autogen.sh
./configure
make
```

mtview does not need to be installed.

How to run
----------

```
sudo ./tools/mtview /dev/input/event0
```

Substitute the event node with the one for your touchpad device. Note that
mtview uses a kernel grab on the device node and events from that device
will not be delivered to any other process while mtview is running
(including the X server or Wayland).

To terminate, Alt-tab back to the terminal and Ctrl-C the process.

License
-------

mtview is licensed under the GPLv3. See LICENSE for details.
