#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

#ifdef _WIN32
using NativeChannelHandle = void *;
inline constexpr NativeChannelHandle invalid_channel_handle = nullptr;
#else
using NativeChannelHandle = int;
inline constexpr NativeChannelHandle invalid_channel_handle = -1;
#endif

enum class ChannelError {
    invalid_token,
    invalid_handle,
    create_failed,
    configure_failed,
    io_failed,
};

enum class ChannelReadResult { data, end_of_stream, failed };

class OwnedChannelHandle final {
  public:
    OwnedChannelHandle() noexcept = default;
    explicit OwnedChannelHandle(NativeChannelHandle handle) noexcept : handle_(handle) {}
    ~OwnedChannelHandle();

    OwnedChannelHandle(const OwnedChannelHandle &) = delete;
    OwnedChannelHandle &operator=(const OwnedChannelHandle &) = delete;
    OwnedChannelHandle(OwnedChannelHandle &&other) noexcept;
    OwnedChannelHandle &operator=(OwnedChannelHandle &&other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] NativeChannelHandle get() const noexcept { return handle_; }
    [[nodiscard]] NativeChannelHandle release() noexcept;
    void reset(NativeChannelHandle replacement = invalid_channel_handle) noexcept;

  private:
    NativeChannelHandle handle_{invalid_channel_handle};
};

struct InheritedDataChannel {
    OwnedChannelHandle supervisor_read;
    OwnedChannelHandle runtime_write;
};

struct InheritedControlChannel {
    OwnedChannelHandle supervisor_write;
    OwnedChannelHandle runtime_read;
};

using ChannelCreateResult = std::variant<InheritedDataChannel, ChannelError>;
using ControlChannelCreateResult = std::variant<InheritedControlChannel, ChannelError>;
using ChannelAdoptResult = std::variant<OwnedChannelHandle, ChannelError>;

[[nodiscard]] ChannelCreateResult create_inherited_data_channel() noexcept;
[[nodiscard]] ControlChannelCreateResult create_inherited_control_channel() noexcept;
[[nodiscard]] ChannelAdoptResult adopt_inherited_data_channel(std::string_view token) noexcept;
[[nodiscard]] std::string inherited_data_channel_token(NativeChannelHandle handle);
[[nodiscard]] bool channel_handle_is_inheritable(NativeChannelHandle handle) noexcept;
[[nodiscard]] bool write_channel(OwnedChannelHandle &channel,
                                 std::span<const std::byte> bytes) noexcept;
// Spec 002 (contracts.md C1): whether a read would not block. `pending` when bytes wait,
// `closed` when the writer is gone, `idle` otherwise.
enum class ChannelPollResult { idle, pending, closed, failed };
[[nodiscard]] ChannelPollResult poll_channel(OwnedChannelHandle &channel) noexcept;
[[nodiscard]] ChannelReadResult read_channel(OwnedChannelHandle &channel,
                                             std::span<std::byte> buffer,
                                             std::size_t &bytes_read) noexcept;

} // namespace ayther::audio_qa
