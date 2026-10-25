#pragma once

#include <cstddef>
#include <cstdint>

#include "gamepad/gamepad.hpp"

namespace SpiProtocol {

inline constexpr std::size_t FrameSize = 32;
inline constexpr std::size_t HeaderSize = 8;
inline constexpr std::size_t MaxPayloadSize = 22;
inline constexpr std::size_t ChecksumSize = 2;
inline constexpr std::uint8_t GamepadCount = 4;

inline constexpr std::uint8_t Magic0 = 'O';
inline constexpr std::uint8_t Magic1 = 'X';
inline constexpr std::uint8_t Version = 2;

enum class MessageType : std::uint8_t {
    GamepadState = 1, // ESP32 to RP2350
    Rumble = 2,       // RP2350 to ESP32
};

enum class FrameError : std::uint8_t {
    None,
    BadMagic,
    UnsupportedVersion,
    UnknownMessageType,
    InvalidPayloadLength,
    InvalidReservedByte,
    InvalidGamepadIndex,
    ChecksumMismatch,
};

struct Frame {
    std::uint8_t bytes[FrameSize];
};

// SPI exchanges are always 32 bytes. Multi-byte values and the CRC are little-endian.
// The CRC-16/CCITT-FALSE covers bytes 0 through 29; the checksum occupies bytes 30-31.
inline constexpr std::size_t PayloadOffset = HeaderSize;
inline constexpr std::size_t ChecksumOffset = FrameSize - ChecksumSize;
// Header byte 7 stores the slot in bits 0-1 and connection state in bit 2.
inline constexpr std::uint8_t GamepadIndexMask = 0x03;
inline constexpr std::uint8_t ConnectedFlag = 0x04;
inline constexpr std::uint8_t ReservedFlagsMask = 0xF8;

static_assert(HeaderSize + MaxPayloadSize == ChecksumOffset);
static_assert(GamepadCount == GamepadIndexMask + 1);
static_assert(sizeof(Frame) == FrameSize);

inline std::uint16_t crc16(const std::uint8_t* data, std::size_t length) noexcept {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) != 0
                ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

inline void writeU16(std::uint8_t* destination, std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8);
}

inline std::uint16_t readU16(const std::uint8_t* source) noexcept {
    return static_cast<std::uint16_t>(source[0])
        | (static_cast<std::uint16_t>(source[1]) << 8);
}

inline std::int16_t readI16(const std::uint8_t* source) noexcept {
    const std::int32_t value = readU16(source);
    return static_cast<std::int16_t>(value >= 0x8000 ? value - 0x10000 : value);
}

inline bool isKnownMessageType(std::uint8_t value) noexcept {
    return value == static_cast<std::uint8_t>(MessageType::GamepadState)
        || value == static_cast<std::uint8_t>(MessageType::Rumble);
}

inline std::uint8_t payloadLength(MessageType type) noexcept {
    switch (type) {
    case MessageType::GamepadState:
        return sizeof(GamepadIn);
    case MessageType::Rumble:
        return sizeof(GamepadOut);
    }
    return 0;
}

inline FrameError finalizeFrame(
    Frame& frame,
    MessageType type,
    std::uint8_t gamepadIndex,
    bool connected,
    std::uint16_t sequence) noexcept {
    if (gamepadIndex >= GamepadCount) {
        for (std::size_t i = 0; i < FrameSize; ++i) {
            frame.bytes[i] = 0;
        }
        return FrameError::InvalidGamepadIndex;
    }

    frame.bytes[0] = Magic0;
    frame.bytes[1] = Magic1;
    frame.bytes[2] = Version;
    frame.bytes[3] = static_cast<std::uint8_t>(type);
    writeU16(&frame.bytes[4], sequence);
    frame.bytes[6] = payloadLength(type);
    frame.bytes[7] = gamepadIndex | (connected ? ConnectedFlag : 0);
    writeU16(&frame.bytes[ChecksumOffset], crc16(frame.bytes, ChecksumOffset));
    return FrameError::None;
}

inline FrameError validateFrame(const Frame& frame, MessageType expectedType) noexcept {
    if (frame.bytes[0] != Magic0 || frame.bytes[1] != Magic1) {
        return FrameError::BadMagic;
    }
    if (frame.bytes[2] != Version) {
        return FrameError::UnsupportedVersion;
    }
    if (!isKnownMessageType(frame.bytes[3])) {
        return FrameError::UnknownMessageType;
    }
    if (frame.bytes[3] != static_cast<std::uint8_t>(expectedType)
        || frame.bytes[6] != payloadLength(expectedType)) {
        return FrameError::InvalidPayloadLength;
    }
    if ((frame.bytes[7] & ReservedFlagsMask) != 0
        || (expectedType == MessageType::Rumble && (frame.bytes[7] & ConnectedFlag) != 0)) {
        return FrameError::InvalidReservedByte;
    }
    if (readU16(&frame.bytes[ChecksumOffset]) != crc16(frame.bytes, ChecksumOffset)) {
        return FrameError::ChecksumMismatch;
    }
    return FrameError::None;
}

inline std::uint8_t readGamepadIndex(const Frame& frame) noexcept {
    return frame.bytes[7] & GamepadIndexMask;
}

inline bool isGamepadConnected(const Frame& frame) noexcept {
    return (frame.bytes[7] & ConnectedFlag) != 0;
}

inline FrameError encodeGamepadState(
    const GamepadIn& state,
    std::uint8_t gamepadIndex,
    bool connected,
    std::uint16_t sequence,
    Frame& frame) noexcept {
    for (std::size_t i = 0; i < FrameSize; ++i) {
        frame.bytes[i] = 0;
    }

    writeU16(&frame.bytes[PayloadOffset], state.Buttons);
    for (std::size_t i = 0; i < static_cast<std::size_t>(GamepadAnalog::Count); ++i) {
        frame.bytes[PayloadOffset + 2 + i] = state.AnalogButtons[i];
    }

    const std::size_t axesOffset = PayloadOffset + 2 + static_cast<std::size_t>(GamepadAnalog::Count);
    writeU16(&frame.bytes[axesOffset], static_cast<std::uint16_t>(state.JoyLx));
    writeU16(&frame.bytes[axesOffset + 2], static_cast<std::uint16_t>(state.JoyLy));
    writeU16(&frame.bytes[axesOffset + 4], static_cast<std::uint16_t>(state.JoyRx));
    writeU16(&frame.bytes[axesOffset + 6], static_cast<std::uint16_t>(state.JoyRy));
    return finalizeFrame(frame, MessageType::GamepadState, gamepadIndex, connected, sequence);
}

inline FrameError decodeGamepadState(
    const Frame& frame,
    GamepadIn& state,
    std::uint8_t& gamepadIndex,
    bool& connected,
    std::uint16_t& sequence) noexcept {
    const FrameError error = validateFrame(frame, MessageType::GamepadState);
    if (error != FrameError::None) {
        return error;
    }

    GamepadIn decoded{};
    decoded.Buttons = readU16(&frame.bytes[PayloadOffset]);
    for (std::size_t i = 0; i < static_cast<std::size_t>(GamepadAnalog::Count); ++i) {
        decoded.AnalogButtons[i] = frame.bytes[PayloadOffset + 2 + i];
    }

    const std::size_t axesOffset = PayloadOffset + 2 + static_cast<std::size_t>(GamepadAnalog::Count);
    decoded.JoyLx = readI16(&frame.bytes[axesOffset]);
    decoded.JoyLy = readI16(&frame.bytes[axesOffset + 2]);
    decoded.JoyRx = readI16(&frame.bytes[axesOffset + 4]);
    decoded.JoyRy = readI16(&frame.bytes[axesOffset + 6]);

    state = decoded;
    gamepadIndex = readGamepadIndex(frame);
    connected = isGamepadConnected(frame);
    sequence = readU16(&frame.bytes[4]);
    return FrameError::None;
}

inline FrameError encodeRumble(
    const GamepadOut& rumble,
    std::uint8_t gamepadIndex,
    std::uint16_t sequence,
    Frame& frame) noexcept {
    for (std::size_t i = 0; i < FrameSize; ++i) {
        frame.bytes[i] = 0;
    }

    writeU16(&frame.bytes[PayloadOffset], rumble.Left);
    writeU16(&frame.bytes[PayloadOffset + 2], rumble.Right);
    return finalizeFrame(frame, MessageType::Rumble, gamepadIndex, false, sequence);
}

inline FrameError decodeRumble(
    const Frame& frame,
    GamepadOut& rumble,
    std::uint8_t& gamepadIndex,
    std::uint16_t& sequence) noexcept {
    const FrameError error = validateFrame(frame, MessageType::Rumble);
    if (error != FrameError::None) {
        return error;
    }

    GamepadOut decoded{
        readU16(&frame.bytes[PayloadOffset]),
        readU16(&frame.bytes[PayloadOffset + 2]),
    };
    rumble = decoded;
    gamepadIndex = readGamepadIndex(frame);
    sequence = readU16(&frame.bytes[4]);
    return FrameError::None;
}

} // namespace SpiProtocol