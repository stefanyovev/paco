
   PortAudio Console (paco)

Goal:
   Repeat Windows audio across different devices in realtime (play all speakers simultaneously).
   Optionally apply effects to the outputs.

How:
   Install VB Audio VB Cable or other virtual audio cable and make it the default windows playback device.
   Make sure the cable and the other devices you want to use are all set to 44100 from windows settings.
   Start this and route cable outputs to devices.

todos:
   uptime prompt
   on resync print src diff
   callback time total, gettime() total?, log callback timestamps & args in file
   desync measure (displacement)
   clear, save (as text), load
   resync btn

Note:
   Even the underrun is somehow handled, overruns may silently delay streams.
   Still necessary to resync every 5-10 minutes ..
