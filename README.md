Signal generator
====
Small generator for square, ramp, triangle or sine functions intended for use with LED's
----
The library allows the user to configure the following items:
- Type of signal: quare, ramp, triangle or sine
- Size of the signal, in samples (limit: 2^24)
- Maximum value for the signal (limit: 2^16)
- Number of channels in the signal, from 1 to 3
- "Color", which is a list of different channel intensities
- Duty cycle, in case the selected signal is "square". Valid values: 0-100
- Intensity of the signal, valid values: 0-100
- Size of the samples, 1 or 2 bytes (in both cases data is treated as unsigned values)
- Option to use logarithmic scale, so that brightness appears to increase linearly

It has been designed to be executed with 32 bit integer multiplications, there are no 64 bit data nor floats.