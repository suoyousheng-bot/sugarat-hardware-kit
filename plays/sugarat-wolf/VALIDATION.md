# Wolf Companion validation

Recorded results for the community release. They describe the recorded build and device only; a rebuilt or modified image needs its own verification.

## Community release notes

Built with ESP-IDF 5.5.4. Version 1.1.0 was flashed as the main application on a Sugarat round badge. Over the serial console, the gaze sweep, tap (think), double tap and shake (dizzy), hold (sofa), doze and wake were triggered and watched on the device. In 10 s windows the average frame time was 7–12 ms and the worst 22–27 ms, with no frame over the 33.3 ms slot, no missed slot and no decode errors. The host compose test matched the reference decode. Installation alongside other plays through the launcher was not tested.
