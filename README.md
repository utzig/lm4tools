lm4tools
========

Some tools which enable multi-platform development on the TI Stellaris Launchpad boards. The Stellaris Launchpad is a low cost development board created by Texas Instruments that comes with an ARM Cortex-M4F processor. You can get one here:

[Get a Stellaris Launchpad](http://www.ti.com/ww/en/launchpad/stellaris_head.html?DCMP=stellaris-launchpad&HQS=stellaris-launchpad-b)

__Setting up a development environment__

First you'll need a cross compiler able to compile for ARM Cortex-Mx aka arm-none-eabi-. Some which are know to work:

* [summon-arm-toolchain by Piotr Esden-Tempski](https://github.com/esden/summon-arm-toolchain)
* [arm-eabi-toolchain by James Snyder](https://github.com/jsnyder/arm-eabi-toolchain)
* [Sourcery Codebench Lite](http://www.mentor.com/embedded-software/sourcery-tools/sourcery-codebench/editions/lite-edition/request?id=e023fac2-e611-476b-a702-90eabb2aeca8&downloadlite=scblite2012&fmpath=/embedded-software/sourcery-tools/sourcery-codebench/editions/lite-edition/form)

More info can be found here [eLinux toolchains](http://elinux.org/Toolchains).

Grab StellarisWare from Texas Instruments: [Stellaris LM4F120 LaunchPad Evaluation Board Software](http://www.ti.com/tool/sw-ek-lm4f120xl). You need to get SW-EK-LM4F120XL.

Inside StellarisWare directory you'll find many examples in the directory *boards/ek-lm4f120xl*. Try building project0 by going to that directory and running *make*.

To flash your Stellaris board from *boards/ek-lm4f120xl/project0*, run:

$ lm4flash gcc/project0.bin

Nice hacking!
