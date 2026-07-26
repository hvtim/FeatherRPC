#pragma once

#include "CliHooks.h"

#include <string>
#include <vector>

namespace cli {

// Parses args (argv[1:], i.e. excluding the program name) and executes the
// matching command against config.json + the injected Hooks. Returns the
// process exit code.
int Run(const std::vector<std::string>& args, Hooks& hooks);

} // namespace cli
