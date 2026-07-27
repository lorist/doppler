/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_PARTICIPANT_LIST_H_
#define _PULSE_PARTICIPANT_LIST_H_

PULSE_DECL_BEGIN

typedef struct _PulseParticipantListSearchOptions PulseParticipantListSearchOptions;

/**
 * @brief Participant list search type bitmask.
 * Defines the search strategies to use when searching a participant list.
 * Values can be combined using bitwise OR.
 */
typedef enum _PulseParticipantListSearchTypeMask
{
  PULSE_SEARCH_PREFIX = 1,
  PULSE_SEARCH_SUBSTRING_IF_EMPTY = 1 << 1,
  PULSE_SEARCH_SUBSTRING_ALWAYS = 1 << 2,
  PULSE_SEARCH_FUZZY_IF_EMPTY = 1 << 3,
  PULSE_SEARCH_FUZZY_ALWAYS = 1 << 4,
} PulseParticipantListSearchTypeMask;

/**
 * @brief Participant list fuzzy search type.
 * Defines the level of fuzziness allowed when performing a fuzzy search.
 */
typedef enum _PulseParticipantListFuzzySearchType
{
  PULSE_FUZZY_SEARCH_ONE_ONE,  /* Allow one char of fuzzyness in one place */
  PULSE_FUZZY_SEARCH_ONE_MANY, /* Allow one char of fuzzyness any number of times */
  PULSE_FUZZY_SEARCH_ANY,      /* Allow any grade of fuzzyness */
} PulseParticipantListFuzzySearchType;

/**
 * @brief Participant list search options.
 * Configuration for how a participant list search should be performed.
 */
struct _PulseParticipantListSearchOptions
{
  PulseParticipantListSearchTypeMask search_mask;
  bool match_case;
  PulseParticipantListFuzzySearchType fuzzy_search_type; /* If fuzzy searching is enabled, this is the type we use */
  uint32_t max_results;
  PulseParticipantListSortingConfig order;
};

/**
 * @brief Search the provided participant list with the given query, returning a new list.
 * Search the provided participant list with the given query, returning a new list.
 * @param list The participant list to perform the search on. This must always be provided.
 * @param query What to search for. If NULL, the call will return a copy of the input list.
 * @param options How the search should be performed. If NULL, the default search mechanism for a roster list will be
 * used.
 * @return A new PulseConferenceEventParticipantList containing the search results, or NULL in case the input list was
 * NULL;
 */

PULSE_EXPORT
PulseConferenceEventParticipantList * pulse_participant_list_search (const PulseConferenceEventParticipantList * list,
                                                                     const char * query,
                                                                     PulseParticipantListSearchOptions * options);

PULSE_DECL_END

#endif