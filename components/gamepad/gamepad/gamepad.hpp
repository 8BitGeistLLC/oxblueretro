#pragma once

#include <cstddef>
#include <cstdint>

namespace GamepadButton {
    inline constexpr uint16_t DpadUp = 1 << 0;
    inline constexpr uint16_t DpadDown = 1 << 1;
    inline constexpr uint16_t DpadLeft = 1 << 2;
    inline constexpr uint16_t DpadRight = 1 << 3;
    inline constexpr uint16_t Start = 1 << 4;
    inline constexpr uint16_t Back = 1 << 5;
    inline constexpr uint16_t L3 = 1 << 6;
    inline constexpr uint16_t R3 = 1 << 7;
}

enum class GamepadAnalog : uint8_t {
    A,
    B,
    X,
    Y,
    Black,
    White,
    Lt,
    Rt,
    Count
};

#pragma pack(push, 1)

struct GamepadIn {
    union {
        struct {
            uint16_t DpadUp : 1;
            uint16_t DpadDown : 1;
            uint16_t DpadLeft : 1;
            uint16_t DpadRight : 1;
            uint16_t Start : 1;
            uint16_t Back : 1;
            uint16_t L3 : 1;
            uint16_t R3 : 1;
            uint16_t : 8;
        };
        uint16_t Buttons;
    };
    union {
        struct {
            uint8_t A;
            uint8_t B;
            uint8_t X;
            uint8_t Y;
            uint8_t Black;
            uint8_t White;
            uint8_t Lt;
            uint8_t Rt;
        };
        uint8_t AnalogButtons[static_cast<std::size_t>(GamepadAnalog::Count)];
    };
    int16_t JoyLx;
    int16_t JoyLy;
    int16_t JoyRx;
    int16_t JoyRy;
};
static_assert(sizeof(GamepadIn) == 18);
static_assert(offsetof(GamepadIn, Buttons) == 0);
static_assert(offsetof(GamepadIn, AnalogButtons) == 2);
static_assert(offsetof(GamepadIn, JoyLx) == 10);
static_assert(offsetof(GamepadIn, JoyLy) == 12);
static_assert(offsetof(GamepadIn, JoyRx) == 14);
static_assert(offsetof(GamepadIn, JoyRy) == 16);

struct GamepadOut {
    uint16_t Left;
    uint16_t Right;
};
static_assert(sizeof(GamepadOut) == 4);
static_assert(offsetof(GamepadOut, Left) == 0);
static_assert(offsetof(GamepadOut, Right) == 2);

#pragma pack(pop)
