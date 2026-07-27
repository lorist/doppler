/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_REGISTRATIONS_EVENT_H_
#define _PULSE_REGISTRATIONS_EVENT_H_

/**
 * @brief Registration event types delivered to the registration event callback.
 */
typedef enum
{
  PULSE_REGISTRATIONS_EVENT_TYPE_HELLO,
  PULSE_REGISTRATIONS_EVENT_TYPE_BYE,
  PULSE_REGISTRATIONS_EVENT_TYPE_INCOMING,
  PULSE_REGISTRATIONS_EVENT_TYPE_INCOMING_CANCELLED,
  PULSE_REGISTRATIONS_EVENT_TYPE_REFRESH_TOKEN,
  __PULSE_REGISTRATIONS_EVENT_TYPE_SIZE__
} PulseRegistrationsEventType;

/**
 * @brief Source type for an incoming call on a registration.
 */
typedef enum
{
  PULSE_REGISTRATION_INCOMING_CALL_SOURCE_DEVICE,
  PULSE_REGISTRATION_INCOMING_CALL_SOURCE_CONFERENCE,
} PulseRegistrationIncomingCallSourceType;

#endif /* _PULSE_REGISTRATIONS_EVENT_H_ */