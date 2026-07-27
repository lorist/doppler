/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_FECC_TYPES_H_
#define _PULSE_FECC_TYPES_H_

/**
 * @brief Far-End Camera Control (FECC) action type.
 * Indicates whether a camera movement is starting, continuing, or stopping.
 */
typedef enum
{
  PULSE_FECC_ACTION_START = 0,
  PULSE_FECC_ACTION_CONTINUE = 1,
  PULSE_FECC_ACTION_STOP = 2,
} PulseFeccActionType;

/**
 * @brief Far-End Camera Control (FECC) movement direction bitmask.
 * Flags may be combined to describe simultaneous pan/tilt/zoom movements.
 */
typedef enum
{
  PULSE_FECC_MOVEMENT_PAN_LEFT = 1 << 0,
  PULSE_FECC_MOVEMENT_PAN_RIGHT = 1 << 1,
  PULSE_FECC_MOVEMENT_TILT_UP = 1 << 2,
  PULSE_FECC_MOVEMENT_TILT_DOWN = 1 << 3,
  PULSE_FECC_MOVEMENT_ZOOM_IN = 1 << 4,
  PULSE_FECC_MOVEMENT_ZOOM_OUT = 1 << 5,
} PulseFeccMovementDirection;

#endif /* _PULSE_FECC_TYPES_H_ */