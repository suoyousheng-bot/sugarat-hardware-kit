# Sugarat Armor

The Sugarat ESP32-S3 round badge controls the Armor Fly browser game over Bluetooth. The badge shows connection instructions until the browser subscribes, then displays a full-screen animated reactor. Open the game in desktop Chrome or Edge on localhost or HTTPS, select Connect Sugarat, then choose Sugarat Armor. Swipe up from the bottom to open the controls. Hold the badge screen up with the arrow edge, marked TO SCREEN, toward the computer. Flying starts as soon as the browser connects, with the badge lying flat as neutral; no centering step is needed. Tap Center in the control sheet to make the current hold neutral instead. Swipe down or tap outside to close the controls. Lower the left or right edge to turn. Lift the arrow edge to climb and lower it to descend. Turning the level badge on its own plane leaves the flight controls centered. Flight input starts beyond 2° from neutral and reaches full turn at 30° or full climb at 25°; tilting further holds full input, and input stops only when the motion sensor stops reporting. The badge reads tilt from the gyro, corrected toward gravity over 0.25 s, sends 50 packets per second, and asks the computer for a 15–30 ms Bluetooth connection interval. Hold Boost for thrust and tap View to cycle the browser camera.

## Build

Run `idf.py build` with ESP-IDF 5.5.4. Install the application image `build/sugarat_armor.bin` through the Sugarat launcher for a multi-app badge.

## Bluetooth

The service UUID is `735a4001-6dc2-4144-8a01-dc38c5469120` and the notify characteristic is `735a4002-6dc2-4144-8a01-dc38c5469120`. The 10-byte packet holds protocol version 1, flags (ready and boost), a sequence number, signed turn and climb axes scaled by 1000, and the cumulative View count. All fields after the first two bytes are 16-bit little endian. The badge sends controls every 20 ms while subscribed. The page clears input on a pause, disconnect, or 500 ms without a valid packet.
