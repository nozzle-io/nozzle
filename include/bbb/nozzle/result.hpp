#pragma once

#include <string>
#include <utility>

namespace bbb {
namespace nozzle {

enum class ErrorCode {
    Ok,
    Unknown,
    InvalidArgument,
    UnsupportedBackend,
    UnsupportedFormat,
    DeviceMismatch,
    ResourceCreationFailed,
    SharedHandleFailed,
    SenderNotFound,
    SenderClosed,
    Timeout,
    BackendError,
};

struct Error {
    ErrorCode code{ErrorCode::Unknown};
    std::string message{};

    Error() = default;
    Error(ErrorCode code, std::string message)
        : code{code}
        , message{std::move(message)}
    {}
};

template <typename T>
class Result {
public:
    Result(T value)
        : has_value_{true}
        , value_{std::move(value)}
        , error_{}
    {}

    Result(Error error)
        : has_value_{false}
        , value_{}
        , error_{std::move(error)}
    {}

    Result(const Result &) = delete;
    Result &operator=(const Result &) = delete;

    Result(Result &&other) noexcept
        : has_value_{other.has_value_}
        , value_{std::move(other.value_)}
        , error_{std::move(other.error_)}
    {
        other.has_value_ = false;
    }

    Result &operator=(Result &&other) noexcept {
        if (this != &other) {
            has_value_ = other.has_value_;
            value_ = std::move(other.value_);
            error_ = std::move(other.error_);
            other.has_value_ = false;
        }
        return *this;
    }

    ~Result() = default;

    bool ok() const noexcept {
        return has_value_;
    }

    explicit operator bool() const noexcept {
        return has_value_;
    }

    const T &value() const noexcept {
        return value_;
    }

    const T &operator*() const noexcept {
        return value_;
    }

    const T *operator->() const noexcept {
        return &value_;
    }

    T &value() noexcept {
        return value_;
    }

    T &operator*() noexcept {
        return value_;
    }

    T *operator->() noexcept {
        return &value_;
    }

    const Error &error() const noexcept {
        return error_;
    }

private:
    bool has_value_{false};
    T value_{};
    Error error_{};
};

template <>
class Result<void> {
public:
    Result()
        : has_value_{true}
        , error_{}
    {}

    Result(Error error)
        : has_value_{false}
        , error_{std::move(error)}
    {}

    Result(const Result &) = delete;
    Result &operator=(const Result &) = delete;

    Result(Result &&other) noexcept
        : has_value_{other.has_value_}
        , error_{std::move(other.error_)}
    {
        other.has_value_ = false;
    }

    Result &operator=(Result &&other) noexcept {
        if (this != &other) {
            has_value_ = other.has_value_;
            error_ = std::move(other.error_);
            other.has_value_ = false;
        }
        return *this;
    }

    ~Result() = default;

    bool ok() const noexcept {
        return has_value_;
    }

    explicit operator bool() const noexcept {
        return has_value_;
    }

    const Error &error() const noexcept {
        return error_;
    }

private:
    bool has_value_{false};
    Error error_{};
};

} // namespace nozzle
} // namespace bbb
