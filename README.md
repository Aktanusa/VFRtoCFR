# VFRtoCFR
Converts a variable frame rate (VFR) video to a constant frame rate (CFR) video with the help of Matroska Version 2 Timecodes.

Usage:

VFRtoCFR(clip c, string times="times.txt", int numfps=30000, int denfps=1001, bool dropped=false)

Where:

c	The clip to convert from VFR to CFR
times	The path to  Matroska timecodes (v2)
numfps	The numerator of the CFR
denfps	The denominator of the CFR
dropped	If true, it will throws an error if there are frames dropped in the conversion.  Good to figure out if the CFR is set too low.

Changelog:

2018/01/18:
	- Minor text change
	- Compiled 64-bit DLL

2012/07/30:
	- Rewrote the whole algorithm to be a lot smarter

2012/05/28:
	- Initial release
