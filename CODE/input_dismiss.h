#pragma once

namespace InputDismiss {

using TriggerEnabledCallback = void (*)(bool enabled);

bool Start(TriggerEnabledCallback set_trigger_enabled);
void Stop();

} // namespace InputDismiss
