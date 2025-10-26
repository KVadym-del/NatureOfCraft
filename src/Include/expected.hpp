#include "core.hpp"

#include <expected>
#include <string_view>

enum class ErrorCode
{
    None,
};

struct Error
{
    std::string_view message{};
    ErrorCode code{ErrorCode::None};
};

template <typename T = void> using Result = std::expected<T, Error>;

constexpr static auto make_error(std::string_view message, ErrorCode code = ErrorCode::None)
{
    return std::unexpected(Error{message, code});
}
