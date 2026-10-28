#include "platform/MediaControls.h"

namespace newamp {

std::unique_ptr<MediaControlsBackend> MediaControlsBackend::create(PlayerController &, const std::function<void()> &)
{
    return nullptr;
}

} // namespace newamp
