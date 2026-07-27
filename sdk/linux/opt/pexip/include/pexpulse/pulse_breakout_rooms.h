/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_BREAKOUT_ROOMS_H_
#define _PULSE_BREAKOUT_ROOMS_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Opaque room identifier type. */
typedef uint32_t PulseRoomId;

/** @brief Room ID for the main room. */
#define PULSE_ROOM_ID_MAIN 0

/** @brief Room ID referring to the previously active room. */
#define PULSE_ROOM_ID_PREVIOUS ((uint32_t)-1)

#endif /* _PULSE_BREAKOUT_ROOMS_H_ */
