#pragma once
// Network provisioning data in its own NVS namespace ("pcd_net"), separate from UI settings.
// NOTE: NVS is NOT encrypted on this build (no flash-encryption / NVS keys provisioned), so the
// Wi-Fi password is readable by anyone with physical access to the flash. See PC-Control-Deck-Agent docs/SECURITY.md.
// Never log `pass`.

#include <stdint.h>

namespace storage {

struct NetConfig {
  char ssid[33] = "";
  char pass[65] = "";
  char host[64] = "";  // agent IPv4 / hostname
  uint16_t port = 8765;
  // Pairing result (Hito 4). The token is a device credential issued by the agent: never logged.
  char token[72] = "";
  char agent_id[24] = "";   // stable agent identity (also advertised over mDNS)
  char agent_name[40] = ""; // PC name shown to the user

  bool has_wifi() const { return ssid[0] != '\0'; }
  bool has_agent() const { return host[0] != '\0' && port != 0; }
  bool paired() const { return token[0] != '\0'; }
};

bool load_net(NetConfig &out);
bool save_net(const NetConfig &c);
bool clear_net();

}  // namespace storage
