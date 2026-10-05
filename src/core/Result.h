#pragma once
#include <stdexcept>
#include <string>
#include <variant>
namespace drawborad {
enum class ErrorCode {
    InvalidInput,
    Overflow,
    BudgetExceeded,
    ProfileRequired,
    InvalidProfile,
    Locked,
    Cycle,
    StaleRevision,
    Cancelled,
    Timeout,
    DeviceLost,
    UnsupportedFeature,
    UnsupportedAdapter,
    ConversionConsentRequired,
    MissingDigest,
    DigestMismatch,
    CorruptData,
    PathEscape,
    IoError,
    LicenseBlocked
};
struct Error {
    ErrorCode code;
    std::string messageKey;
    std::string diagnosticId;
    bool operator==(const Error&) const = default;
};
inline Error error(ErrorCode c, std::string key) {
    return {c, std::move(key), {}};
}
template <class T> class [[nodiscard]] Result {
    std::variant<T, Error> v_;

  public:
    Result(T v) : v_(std::move(v)) {}
    Result(Error e) : v_(std::move(e)) {}
    explicit operator bool() const noexcept {
        return std::holds_alternative<T>(v_);
    }
    T& value() {
        if (!*this)
            throw std::logic_error("Result has no value");
        return std::get<T>(v_);
    }
    const T& value() const {
        if (!*this)
            throw std::logic_error("Result has no value");
        return std::get<T>(v_);
    }
    const Error& error() const {
        if (*this)
            throw std::logic_error("Result has no error");
        return std::get<Error>(v_);
    }
};
template <> class [[nodiscard]] Result<void> {
    std::variant<std::monostate, Error> v_;

  public:
    Result() = default;
    Result(Error e) : v_(std::move(e)) {}
    explicit operator bool() const noexcept {
        return std::holds_alternative<std::monostate>(v_);
    }
    void value() const {
        if (!*this)
            throw std::logic_error("Result has no value");
    }
    const Error& error() const {
        if (*this)
            throw std::logic_error("Result has no error");
        return std::get<Error>(v_);
    }
};
} // namespace drawborad
