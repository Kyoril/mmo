// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/movement_info.h"

namespace mmo
{
    /// @brief Types of movement events that can be generated
    namespace movement_event_type
    {
        enum Type
        {
            /// @brief Heartbeat event sent periodically while moving
            Heartbeat,
            /// @brief Player started falling (or jumping)
            Fall,
            /// @brief Player landed (stopped falling)
            Land,
            /// @brief Player started moving forward
            StartMoveForward,
            /// @brief Player started moving backward
            StartMoveBackward,
            /// @brief Player stopped moving
            StopMove,
            /// @brief Player started strafing left
            StartStrafeLeft,
            /// @brief Player started strafing right
            StartStrafeRight,
            /// @brief Player stopped strafing
            StopStrafe,
            /// @brief Player started turning left
            StartTurnLeft,
            /// @brief Player started turning right
            StartTurnRight,
            /// @brief Player stopped turning
            StopTurn,
            /// @brief Player set facing direction
            SetFacing,

            Count_
        };
    }

    typedef movement_event_type::Type MovementEventType;

    /// @brief A movement event containing timestamp and movement state
    struct MovementEvent
    {
        /// @brief The type of movement event
        MovementEventType eventType;

        /// @brief Timestamp when the event occurred
        GameTime timestamp;

        /// @brief Complete movement info at the time of the event
        MovementInfo movementInfo;

        /// @brief Default constructor
        MovementEvent() = default;

        /// @brief Constructor with parameters
        /// @param type The event type
        /// @param time The timestamp
        /// @param info The movement information
        MovementEvent(MovementEventType type, GameTime time, const MovementInfo& info)
            : eventType(type)
            , timestamp(time)
            , movementInfo(info)
        {
        }
    };
}
