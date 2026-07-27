/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_CONFIG_H_
#define _PULSE_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Marks a symbol for export from the Pulse shared library.
 *
 * On MSVC non-static builds this resolves to __declspec(dllexport/dllimport).
 * On GCC/Clang it sets default visibility. Otherwise it is a plain extern.
 */
#if defined(_MSC_VER) && !(PULSE_STATIC_BUILD)
#ifdef PULSE_EXPORTS
#define PULSE_EXPORT __declspec (dllexport)
#else
#define PULSE_EXPORT __declspec (dllimport) extern
#endif
#else
#if defined(__GNUC__) || defined(__clang__)
#define PULSE_EXPORT extern __attribute__ ((visibility ("default")))
#else
#define PULSE_EXPORT extern
#endif
#endif

/** @brief Opens an extern "C" block when compiling as C++; no-op in C. */
#ifdef __cplusplus
#define PULSE_DECL_BEGIN                                                                                               \
  extern "C"                                                                                                           \
  {
/** @brief Closes the extern "C" block opened by PULSE_DECL_BEGIN. */
#define PULSE_DECL_END }
#else
#define PULSE_DECL_BEGIN
#define PULSE_DECL_END
#endif

#endif /* _PULSE_CONFIG_H_ */
