#pragma once

#include "media_sdk/Error.h"

#include <optional>
#include <utility>

namespace media_sdk {

template<typename T>
class [[nodiscard("Result must be inspected for success or failure")]] Result
{
public:
    static Result success(T value)
    {
        return Result(std::move(value));
    }

    static Result failure(MediaError error)
    {
        return Result(std::move(error));
    }

    bool ok() const { return m_value.has_value(); }
    explicit operator bool() const { return ok(); }

    const T& value() const { return *m_value; }
    T& value() { return *m_value; }

    const MediaError& error() const { return m_error; }

private:
    explicit Result(T value)
        : m_value(std::move(value))
    {
    }

    explicit Result(MediaError error)
        : m_error(std::move(error))
    {
    }

    std::optional<T> m_value;
    MediaError m_error;
};

template<>
class [[nodiscard("Result must be inspected for success or failure")]] Result<void>
{
public:
    static Result success()
    {
        return Result(true, {});
    }

    static Result failure(MediaError error)
    {
        return Result(false, std::move(error));
    }

    bool ok() const { return m_ok; }
    explicit operator bool() const { return ok(); }

    const MediaError& error() const { return m_error; }

private:
    Result(bool ok, MediaError error)
        : m_ok(ok)
        , m_error(std::move(error))
    {
    }

    bool m_ok = false;
    MediaError m_error;
};

} // namespace media_sdk
