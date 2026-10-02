#pragma once

#include "model.h"
#include "model_limits.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

enum class CodecError { too_large, malformed_document, incompatible_version, invalid_field };
template <class T> using DecodeResult = std::variant<T, CodecError>;
using EncodeResult = std::variant<std::string, CodecError>;

[[nodiscard]] EncodeResult to_toml(const Request &request);
[[nodiscard]] EncodeResult to_toml(const Run &run);
[[nodiscard]] DecodeResult<Request> request_from_toml(std::string_view text);
[[nodiscard]] DecodeResult<Run> run_from_toml(std::string_view text);

} // namespace ayther::audio_qa
