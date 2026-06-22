#include "media_sdk/Player.h"

#include <utility>

namespace media_sdk {

class Player::Impl
{
public:
    Impl(PlayerConfig config, IEventSink& events)
        : config(std::move(config))
        , events(events)
    {
    }

    PlayerConfig config;
    IEventSink& events;
};

Player::Player(PlayerConfig config, IEventSink& events)
    : m_impl(std::make_unique<Impl>(std::move(config), events))
{
}

Player::~Player() = default;
Player::Player(Player&&) noexcept = default;
Player& Player::operator=(Player&&) noexcept = default;

Result<void> Player::open(const std::filesystem::path&)
{
    return Result<void>::success();
}

void Player::play()
{
}

void Player::pause()
{
}

void Player::stop()
{
}

Result<void> Player::seek(std::chrono::milliseconds)
{
    return Result<void>::success();
}

} // namespace media_sdk
