/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_DEVICE_H_
#define _PULSE_DEVICE_H_

#include "pulse_error.h"
#include "pulse_media.h"

// Primitives
typedef uint32_t PulseDeviceID; // --> PmxDeviceID;

// Opaque types
typedef struct _PulseDevice PulseDevice;
typedef struct _PulseDeviceList PulseDeviceList;
typedef struct _PulseDeviceIterator PulseDeviceIterator;

// Function pointer types
typedef void (*PulseDeviceIteratorFunc) (const PulseDevice * device, void * user_context);
typedef void (*PulseDeviceListChangeFunc) (PulseMediaType media_type, void * user_context);

typedef void (*PulseDeviceErrorFunc) (void * user_context, PulseDeviceID device_id, PulseError device_error);

typedef struct
{
  uint8_t level;
  int voice_activity;

} PulseAudioLevelMeta;

typedef struct
{
  const PulseAudioLevelMeta * metas;
  size_t len;

} PulseAudioLevelMetasList;

typedef void (*PulseDeviceAudioLevelFunc) (void * user_context, const PulseAudioLevelMetasList * list);

PULSE_DECL_BEGIN

/*
  PULSE DEVICE CHANGE CALLBACKS
*/

PULSE_EXPORT
PulseError pulse_register_device_list_changed_callback (Pulse * client, PulseMediaType media_type,
                                                        PulseDeviceListChangeFunc func, void * user_context);

PULSE_EXPORT
PulseError pulse_deregister_device_list_changed_callback (Pulse * client, PulseMediaType media_type);

PULSE_EXPORT
PulseError pulse_register_device_error_callback (Pulse * client, PulseDeviceErrorFunc func, void * user_context);

PULSE_EXPORT
PulseError pulse_deregister_device_error_callback (Pulse * client);

PULSE_EXPORT
PulseError pulse_register_device_audio_level_callback (Pulse * client, PulseMediaDirection direction,
                                                       uint32_t window_size, PulseDeviceAudioLevelFunc func,
                                                       void * user_context);

PULSE_EXPORT
PulseError pulse_deregister_device_audio_level_callback (Pulse * client, PulseMediaDirection direction);

/*
  PULSE DEVICE ITERATOR
*/

/**
 * @brief Create a new device iterator.
 * Creates a new device iterator for the specified media_type and direction.
 * @param client The Pulse handle
 * @param media_type The kind of media device we want to list. Either PULSE_MEDIA_AUDIO or PULSE_MEDIA_VIDEO.
 * @param media_direction Iterate input or output devices?
 * @param iterator Reference to a PulseDeviceIterator pointer, that will be used as handle for any subsequent calls to
 * the iteration functions.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_device_iterator_new (Pulse * client, PulseMediaType media_type, PulseMediaDirection media_direction,
                                      PulseDeviceIterator ** iterator);

/**
 * @brief Free a previously created device iterator.
 * Frees any data belonging to the referenced device iterator.
 * @param iterator The device iterator handle.
 */
PULSE_EXPORT
void pulse_device_iterator_free (PulseDeviceIterator * iterator);

/**
 * @brief Get the number of devices in an iterator.
 * Returns the number of devices identified by referenced device iterator handle.
 * @param iterator The device iterator handle.
 * @return The device count.
 */
PULSE_EXPORT
int pulse_device_iterator_item_count (PulseDeviceIterator * iterator);

/**
 * @brief Get the first registered device
 * Resets the iteration state, and returns the first device.
 * @param iterator The device iterator handle.
 * @return A reference to a const PulseDevice instance owned by the iterator. The reference is unique to every device,
 * and can safely be referenced as long as the iterator is not freed.
 */
PULSE_EXPORT
const PulseDevice * pulse_device_iterator_first (PulseDeviceIterator * iterator);

/**
 * @brief Get the next registered device
 * Returns the next device in the iteration chain.
 * @param iterator The device iterator handle.
 * @return A reference to a const PulseDevice instance owned by the iterator. The reference is unique to every device,
 * and can safely be referenced as long as the iterator is not freed.
 */
PULSE_EXPORT
const PulseDevice * pulse_device_iterator_next (PulseDeviceIterator * iterator);

/**
 * @brief Call a function for every device in the iterator.
 * Call a function for every device in the iterator.
 * @param iterator The device iterator handle.
 * @param func The function to call
 * @param user_context Caller context.
 * @return PULSE_SUCCESS (0) on success, or a PulseError code in case of a failure.
 */
PULSE_EXPORT
PulseError pulse_device_iterator_foreach (PulseDeviceIterator * iterator, PulseDeviceIteratorFunc func,
                                          void * user_context);

/*
  PULSE DEVICE
*/

/**
 * @brief Get the name of a PulseDevice
 * Get the name registered to a PulseDevice.
 * @param device A PulseDevice
 * @return A const char * to the name, or NULL if an error occured.
 */
PULSE_EXPORT
const char * pulse_device_get_name (const PulseDevice * device);

/**
 * @brief Get the PulseDeviceID of a PulseDevice
 * Get the PulseDeviceID registered to a PulseDevice.
 * @param device A PulseDevice
 * @return The PulseDeviceID of a device.
 */
PULSE_EXPORT
PulseDeviceID pulse_device_get_id (const PulseDevice * device);

/**
 * @brief Return whether the device is the system default or not.
 * Get the PulseDeviceID registered to a PulseDevice.
 * @param device A PulseDevice
 * @return Boolean indicating whether the device is the system default for its class.
 */
PULSE_EXPORT
bool pulse_device_is_system_default (const PulseDevice * device);

/**
 * @brief Make a copy of a const PulseDeviceID.
 * If there is a need of making application side lists of devices returned from an iterator, call this function to make
 * a copy. The copy should later be freed, by calling pulse_device_free().
 * @param device A PulseDevice.
 * @return A pointer to a copy of the PulseDevice.
 */
PULSE_EXPORT
PulseDevice * pulse_device_copy (const PulseDevice * device);

/**
 * @brief Free any data that was allocated in pulse_device_copy().
 * Free any data that was allocated in pulse_device_copy().
 * @param device A PulseDevice.
 */
PULSE_EXPORT
void pulse_device_free (PulseDevice * device);

PULSE_DECL_END

#endif /* _PULSE_DEVICE_H_ */
