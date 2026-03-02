#pragma once

#include "QingLongClaw/auth.h"
#include "QingLongClaw/config.h"

namespace QingLongClaw {

int run_gateway(Config* config, AuthStore* auth_store, bool debug_mode);

}  // namespace QingLongClaw

