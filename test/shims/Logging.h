#pragma once

// Host-side Logging shim: the production LOG_DBG / LOG_ERR / LOG_INF macros fan out to
// Serial and various Arduino glue. For host tests we just swallow the calls — asserting
// on observable return values is the point of a unit test, not log output.

#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_ERR(...) ((void)0)
