  libPortAudio Console (paco)

Goal:
  Repeat Windows audio across different devices in realtime.
  Optionally apply effects to the outputs.

How:
  Install VB Audio VB Cable or other virtual audio cable and make it the default windows playback device.
  Make sure the cable and the other devices you want to use are all set to 44100 from windows settings.
  Start this and route cable outputs to devices.

todos:
	uptime prompt / global now
	status cmd
	memset unused outs
	callback time total, gettime() total?
