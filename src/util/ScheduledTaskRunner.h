#pragma once

namespace ScheduledTaskRunner {
// Runs all enabled scheduled tasks headlessly (no display).
// Connects to WiFi if needed, executes tasks, disconnects.
void run();
}  // namespace ScheduledTaskRunner
