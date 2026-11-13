#pragma once
// USB-serial provisioning console (spec §6.A: avoid a tiny on-screen keyboard).
// Polled from the UI task; only reachable by someone with a USB cable on the device.
//
//   help                      list commands
//   status                    network + link status (password never shown)
//   wifi <ssid> <password>    save Wi-Fi credentials (quote the SSID if it has spaces)
//   agent <host> [port]       save agent address (default port 8765)
//   demo on|off               switch demo mode (off = use the network)
//   net clear                 forget Wi-Fi + agent
//   reboot

namespace console {
void poll();  // non-blocking; call from loop()
}
