lm4f120icdi
===========

This is a kernel extension that is codeless and just makes sure that
no other kernel extensions load when plugging in an TI Stellaris Launchpad.

# Building and installing

1. Open the project in Xcode 4.x
2. Click the run button
3. Show in Finder the lm4f120icdi.kext that was created (under products), and
   drag it to your desktop.
4. Open Terminal.app

        sudo su
        cd /System/Library/Extensions
        cp -R /User/<username>/Desktop/lm4f120icdi.kext .
        chown -R root:wheel lm4f120icdi.kext
        chmod -R 755 lm4f120icdi.kext

5. Reboot your system

# known issues

Since this overides the AppleCDC driver from taking ownership of the device
the CDC functionality is not available. This means that the serial port
running on the USB connector will not be available.

---

This kext is based on [ez430rf2500][1].

[1]: https://github.com/colossaldynamics/ez430rf2500
