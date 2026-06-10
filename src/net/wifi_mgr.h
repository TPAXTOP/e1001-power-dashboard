#pragma once

#include "../store/config_store.h"

namespace wifi_mgr {

// Blocking connect with timeout from cfg. Returns true when got IP.
bool connect(const Config& cfg);
void disconnect();

}  // namespace wifi_mgr
