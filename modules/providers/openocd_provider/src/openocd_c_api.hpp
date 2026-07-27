#ifndef TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_C_API_HPP_
#define TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_C_API_HPP_

// Internal to this module's src/: nothing outside openocd_provider ever sees
// a raw OpenOCD type. None of these headers declare extern "C" themselves
// (see the fork's CLAUDE.md audit), so every OpenOCD symbol this module
// touches is pulled in through here.
extern "C" {
#include <helper/command.h>
#include <helper/configuration.h>
#include <helper/log.h>
#include <helper/util.h>

#include <jtag/adapter.h>
#include <jtag/jtag.h>

#include <transport/transport.h>

#include <target/target.h>
#include <target/arm_adi_v5.h>
#include <target/arm_cti.h>
#include <target/arm_tpiu_swo.h>
#include <target/arm_tmc.h>
#include <target/arm_etmv4.h>

#include <flash/nor/core.h>
#include <flash/nand/core.h>

#include <pld/pld.h>

#include <rtt/rtt.h>

#include <server/server.h>
#include <server/gdb_server.h>
#include <server/rtt_server.h>
}

#endif  // TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_C_API_HPP_
