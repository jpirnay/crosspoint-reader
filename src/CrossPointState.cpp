#include "CrossPointState.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

namespace {
constexpr char STATE_FILE_JSON[] = "/.crosspoint/state.json";
}  // namespace

CrossPointState CrossPointState::instance;

bool CrossPointState::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveState(*this, STATE_FILE_JSON);
}

bool CrossPointState::loadFromFile() {
  if (Storage.exists(STATE_FILE_JSON)) {
    String json = Storage.readFile(STATE_FILE_JSON);
    if (!json.isEmpty()) {
      return JsonSettingsIO::loadState(*this, json.c_str());
    }
  }
  return false;
}
