## RUN CODE WITH THESE FLAGS TO FIX ANY SOFTWARE CAUSED/TIMING FLICKERS

test

``sudo ./demo --led-rows=32 --led-cols=64 --led-chain=3 --led-parallel=2 --led-slowdown-gpio=3 --led-pwm-bits=7 --led-pwm-lsb-nanoseconds=50 --led-pwm-dither-bits=1 -D 12``





sudo ./demo --led-no-hardware-pulse --led-rows=32 --led-cols=64 --led-chain=3 --led-parallel=2 -D 12

sudo ./demo --led-no-hardware-pulse -D 12


this command ispretty much perfect:

sudo ./demo --led-rows=32 --led-cols=64 --led-chain=3 --led-parallel=2 --led-slowdown-gpio=3 --led-pwm-bits=7 --led-pwm-lsb-nanoseconds=50 --led-pwm-dither-bits=1 -D 12

sudo ./demo --led-rows=32 --led-cols=64 --led-chain=3 --led-parallel=2 --led-slowdown-gpio=4 --led-pwm-bits=7 --led-pwm-lsb-nanoseconds=50 --led-pwm-dither-bits=1 -D 12




RUN CODE WITH THESE FLAGS TO FIX ANY SOFTWARE CAUSED/TIMING FLICKERS

sudo ./demo --led-rows=32 --led-cols=64 --led-chain=3 --led-parallel=2 --led-slowdown-gpio=3 --led-pwm-bits=7 --led-pwm-lsb-nanoseconds=50 --led-pwm-dither-bits=1 -D 12
