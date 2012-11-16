nidaq-tools
===========

simplemeter reads samples from a single analog input (AI) channel, averages a
number of readings, and prints the data to stdout.  See source for supported
options.

Tested on a PCI-6229 with NIDAQmxBase 3.5.0 on OpenSUSE 11.3.


Build
-----

 * Install NIDAQmxBase
 * make
 * ./simplemeter [options..]


Example
-------

  ./simplemeter --chan 4 --mode diff --rate 1000 --avg 500 --rsense 10 --ts

Reads differential samples from channel 4 at 1 kHz, prints averages of 500
points along with the correponding timestamp.  The --rsense option specifies
that we are sampling the voltage across a current sense resistors of 10
mohms, and outputs data in mA. 

Voltage, time, current and resistance are always input and output in milli-
{volts, amps, watts, seconds}.

