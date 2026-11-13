#include "net_store.h"

#include <Preferences.h>
#include <string.h>

namespace storage {
namespace {
const char *kNs = "pcd_net";
}

bool load_net(NetConfig &out) {
  out = NetConfig{};
  Preferences p;
  if (!p.begin(kNs, true)) {
    // First boot: create the namespace so the next read-only opens don't fail (and log) again.
    if (p.begin(kNs, false)) p.end();
    return false;
  }
  p.getString("ssid", out.ssid, sizeof(out.ssid));
  p.getString("pass", out.pass, sizeof(out.pass));
  p.getString("host", out.host, sizeof(out.host));
  out.port = p.getUShort("port", 8765);
  p.getString("token", out.token, sizeof(out.token));
  p.getString("agent_id", out.agent_id, sizeof(out.agent_id));
  p.getString("agent_nm", out.agent_name, sizeof(out.agent_name));
  p.end();
  return true;
}

bool save_net(const NetConfig &c) {
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  p.putString("ssid", c.ssid);
  p.putString("pass", c.pass);
  p.putString("host", c.host);
  p.putUShort("port", c.port);
  p.putString("token", c.token);
  p.putString("agent_id", c.agent_id);
  p.putString("agent_nm", c.agent_name);
  p.end();
  return true;
}

bool clear_net() {
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  const bool ok = p.clear();
  p.end();
  return ok;
}

}  // namespace storage
