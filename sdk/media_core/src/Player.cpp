#include "media_sdk/Player.h"

#include "PlaybackController.h"

#include <utility>

namespace media_sdk {

class Player::Impl
{
public:
    Impl(PlayerConfig config, IEventSink& events)
        : controller(std::move(config), events)
    {
    }

    PlaybackController controller;
};

Player::Player(PlayerConfig config, IEventSink& events)
    : m_impl(std::make_unique<Impl>(std::move(config), events))
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

} // namespace media_sdk
