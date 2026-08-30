# Android app

Open this `android` folder in Android Studio.

Toolchain used by the project:
- Android Gradle Plugin 9.3.0
- Kotlin 2.3.21
- JDK 17
- compileSdk / targetSdk 37
- minSdk 26

The app communicates directly with the ESP32 over HTTP on the local Wi-Fi network.

Because this is a LAN-only embedded-device starter app, `usesCleartextTraffic=true` is enabled.
For Internet/remote access, do **not** expose port 80 directly to the Internet. Prefer VPN (WireGuard/Tailscale), or add authenticated HTTPS through a gateway.

Current v1 app:
- displays both sensor readings and valve state
- manual ON/OFF for each valve
- moisture start/stop thresholds
- max watering duration
- one daily schedule per zone

The firmware supports four schedule slots per zone. The simple v1 Android UI edits only schedule slot 0 and sets it to every day.


## Home Assistant

The Android app can still talk directly to the ESP32 on the LAN. For remote access, use the Home Assistant mobile app through Home Assistant Cloud or your VPN/WireGuard connection rather than exposing the ESP32 API directly.
