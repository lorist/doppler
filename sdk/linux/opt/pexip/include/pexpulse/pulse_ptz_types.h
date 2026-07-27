/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_PTZ_TYPES_H_
#define _PULSE_PTZ_TYPES_H_

/**
 * @brief Pan-Tilt-Zoom axis bitmask.
 * Flags may be combined to indicate which axes are active.
 */
typedef enum
{
  PULSE_PTZ_AXIS_PAN = 1 << 0,
  PULSE_PTZ_AXIS_TILT = 1 << 1,
  PULSE_PTZ_AXIS_ZOOM = 1 << 2,
} PulsePTZAxis;

#endif /* _PULSE_PTZ_TYPES_H_ */