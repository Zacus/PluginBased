#include "media_sdk/Player.h"

#include "PlaybackController.h"

#include <utility>

namespace media_sdk {

class Player::Impl
{
public:
    Impl(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames)
        : controller(std::move(config), events, frames)
    {
    }

    PlaybackController controller;
};

Player::Player(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames)
    : m_impl(std::make_unique<Impl>(std::move(config), events, frames))
{
}

Player::~Player() = default;
Player::Player(Player&&) noexcept = default;
Player& Player::operator=(Player&&) noexcept = default;

Result<void> Player::open(const std::filesystem::path& path)
{
    return m_impl->controller.open(path);
}

void Player::play()
{
    m_impl->controller.play();
}

void Player::pause()
{
    m_impl->controller.pause();
}

void Player::stop()
{
    m_impl->controller.stop();
}

Result<void> Player::seek(std::chrono::milliseconds position)
{
    return m_impl->controller.seek(position);
}

Result<void> Player::seek(std::chrono::milliseconds position, SeekPlaybackMode mode)
{
    return m_impl->controller.seek(position, mode);
}

} // namespace media_sdk
