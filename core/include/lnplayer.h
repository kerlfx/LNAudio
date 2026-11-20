#ifndef LNPLAYER_H
#define LNPLAYER_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
    #if defined(LNPLAYER_BUILD)
        #define LNPLAYER_API __declspec(dllexport)
    #else
        #define LNPLAYER_API __declspec(dllimport)
    #endif
#else
    #define LNPLAYER_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LNPLAYER_ABI_VERSION 1u

typedef struct lnplayer_handle lnplayer_t;

typedef enum lnplayer_result
{
    LNPLAYER_RESULT_OK = 0,
    LNPLAYER_RESULT_INVALID_ARGUMENT = 1,
    LNPLAYER_RESULT_INVALID_HANDLE = 2,
    LNPLAYER_RESULT_ALREADY_EXISTS = 3,
    LNPLAYER_RESULT_INVALID_STATE = 4,
    LNPLAYER_RESULT_NOT_IMPLEMENTED = 5,
    LNPLAYER_RESULT_OUT_OF_MEMORY = 6,
    LNPLAYER_RESULT_INTERNAL_ERROR = 7,
    LNPLAYER_RESULT_BUFFER_TOO_SMALL = 8,
    LNPLAYER_RESULT_WOULD_DEADLOCK = 9
} lnplayer_result_t;

typedef enum lnplayer_state
{
    LNPLAYER_STATE_IDLE = 0,
    LNPLAYER_STATE_OPENING = 1,
    LNPLAYER_STATE_READY = 2,
    LNPLAYER_STATE_PLAYING = 3,
    LNPLAYER_STATE_PAUSED = 4,
    LNPLAYER_STATE_SEEKING = 5,
    LNPLAYER_STATE_ENDED = 6,
    LNPLAYER_STATE_ERROR = 7,
    LNPLAYER_STATE_CLOSING = 8
} lnplayer_state_t;

typedef enum lnplayer_event_type
{
    LNPLAYER_EVENT_OPEN_COMPLETED = 1,
    LNPLAYER_EVENT_SEEK_COMPLETED = 2,
    LNPLAYER_EVENT_STATE_CHANGED = 3,
    LNPLAYER_EVENT_PLAYBACK_ENDED = 4,
    LNPLAYER_EVENT_ERROR = 5
} lnplayer_event_type_t;

typedef struct lnplayer_event
{
    uint32_t struct_size;
    lnplayer_event_type_t type;
    lnplayer_result_t result;
    lnplayer_state_t state;
    uint64_t operation_id;
    uint64_t generation;
    int64_t position_ms;
    int64_t duration_ms;
    const char* message;
} lnplayer_event_t;

/*
 * Event callbacks are invoked on the Core event thread.
 * The event and its message are valid only for the duration of the callback.
 * user_data must remain valid until lnplayer_destroy returns.
 * Callbacks must not invoke play, pause, stop, close, or destroy.
 */
typedef void (*lnplayer_event_callback_t)(
    lnplayer_t* player,
    const lnplayer_event_t* event,
    void* user_data);

typedef struct lnplayer_config
{
    uint32_t struct_size;
    lnplayer_event_callback_t event_callback;
    void* user_data;
} lnplayer_config_t;

typedef struct lnplayer_snapshot
{
    uint32_t struct_size;
    lnplayer_state_t state;
    int64_t position_ms;
    int64_t duration_ms;
    uint32_t volume;
    uint32_t reserved;
    uint64_t generation;
} lnplayer_snapshot_t;

LNPLAYER_API uint32_t lnplayer_get_abi_version(void);

/* One Player instance is allowed in each process. */
LNPLAYER_API lnplayer_result_t lnplayer_create(
    const lnplayer_config_t* config,
    lnplayer_t** player);

/*
 * Synchronous destruction barrier.
 * The caller must prevent concurrent API calls and must not call this from
 * the Player event callback.
 */
LNPLAYER_API lnplayer_result_t lnplayer_destroy(lnplayer_t* player);

/* Submit operations: return after the request has been accepted. */
LNPLAYER_API lnplayer_result_t lnplayer_open(
    lnplayer_t* player,
    const char* source_utf8,
    uint64_t* operation_id);

LNPLAYER_API lnplayer_result_t lnplayer_seek(
    lnplayer_t* player,
    int64_t position_ms,
    uint64_t* operation_id);

/* Completion operations: return only after the operation has completed. */
LNPLAYER_API lnplayer_result_t lnplayer_play(lnplayer_t* player);
LNPLAYER_API lnplayer_result_t lnplayer_pause(lnplayer_t* player);
LNPLAYER_API lnplayer_result_t lnplayer_stop(lnplayer_t* player);
LNPLAYER_API lnplayer_result_t lnplayer_close(lnplayer_t* player);

/* Snapshot queries do not wait for the event loop or worker pool. */
LNPLAYER_API lnplayer_result_t lnplayer_get_snapshot(
    lnplayer_t* player,
    lnplayer_snapshot_t* snapshot);

/* Volume range: 0 (muted) through 1000 (100%). */
LNPLAYER_API lnplayer_result_t lnplayer_set_volume(
    lnplayer_t* player,
    uint32_t volume);

/* Returns the last error on the calling thread; required_size includes NUL. */
LNPLAYER_API lnplayer_result_t lnplayer_get_last_error(
    char* buffer,
    size_t buffer_size,
    size_t* required_size);

#ifdef __cplusplus
}
#endif

#endif
