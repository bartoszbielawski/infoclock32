#pragma once
#include <wrap_client.hpp>

// -----------------------------------------------------------------------
// WRAP/JAPC variable subscriptions
//
// Add WrapParam entries to kWrapSubscriptions to subscribe to JAPC
// parameters. On each update the task stores the value in RuntimeStore
// under WrapParam::key, making it available to custom message {placeholders}
// and to any other task via RuntimeStore::getInstance().get("key").
//
// Fields (all std::string):
//   key       — your identifier; also used as the RuntimeStore key
//   device    — JAPC device name   (e.g. "LHC.BeamMode")
//   property  — JAPC property name (e.g. "Acquisition")
//   field     — field inside the values object to store (e.g. "value")
//   selector  — timing selector; leave "" for broadcast / no selector
//
// Example:
//   { "lhc_mode",  "LHC.BeamMode",       "Acquisition", "value",  "" },
//   { "b1_energy", "LHC.BeamEnergy:B1",   "Acquisition", "energy", "" },
// -----------------------------------------------------------------------

static const WrapParam kWrapSubscriptions[] = {
    // --- add your subscriptions here ---
    // { "lhc_mode", "LHC.BeamMode", "Acquisition", "value", "" },
};

static const size_t kWrapSubscriptionCount =
    sizeof(kWrapSubscriptions) / sizeof(kWrapSubscriptions[0]);
