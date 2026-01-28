/*******************************************************************************
 * h2gis_wrapper.h - Function pointer wrappers for H2GIS API
 * 
 * This file provides wrapper functions that use dlsym to call into libh2gis.so
 * at runtime rather than linking directly. This allows us to:
 * 1. Load libh2gis.so in a thread with large stack for GraalVM
 * 2. Avoid undefined symbol errors when loading the plugin
 ******************************************************************************/

#ifndef H2GIS_WRAPPER_H_INCLUDED
#define H2GIS_WRAPPER_H_INCLUDED

#include <stdint.h>
#include <stddef.h>
#include "graal_isolate.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque types (matching graal_isolate.h)
// graal_isolatethread_t and graal_isolate_t are defined in graal_isolate.h

// Initialize the wrapper (loads libh2gis.so in a thread with large stack)
// Returns 0 on success, -1 on failure
int h2gis_wrapper_init(void);

// Check if wrapper is initialized
int h2gis_wrapper_is_initialized(void);

// Reference counting for proper shutdown
// Call add_ref when opening a datasource, release when closing
void h2gis_wrapper_add_ref(void);
void h2gis_wrapper_release(void);

// Force shutdown (called when last datasource closes)
void h2gis_wrapper_shutdown(void);

// Get the global isolate thread (attaches current thread if needed)
graal_isolatethread_t* h2gis_wrapper_get_thread(void);

// Get the global isolate
graal_isolate_t* h2gis_wrapper_get_isolate(void);

// H2GIS Function wrappers - same signatures as h2gis.h but resolved via dlsym
char* wrap_h2gis_get_last_error(graal_isolatethread_t* thread);
long long int wrap_h2gis_connect(graal_isolatethread_t* thread, char* path, char* user, char* pass);
long long int wrap_h2gis_load(graal_isolatethread_t* thread, long long int conn);
long long int wrap_h2gis_fetch(graal_isolatethread_t* thread, long long int rs, char* sql);
int wrap_h2gis_execute(graal_isolatethread_t* thread, long long int conn, char* sql);
long long int wrap_h2gis_prepare(graal_isolatethread_t* thread, long long int conn, char* sql);
void wrap_h2gis_bind_double(graal_isolatethread_t* thread, long long int stmt, int idx, double val);
void wrap_h2gis_bind_int(graal_isolatethread_t* thread, long long int stmt, int idx, int val);
void wrap_h2gis_bind_long(graal_isolatethread_t* thread, long long int stmt, int idx, long long int val);
void wrap_h2gis_bind_string(graal_isolatethread_t* thread, long long int stmt, int idx, char* val);
void wrap_h2gis_bind_blob(graal_isolatethread_t* thread, long long int stmt, int idx, char* data, int len);
int wrap_h2gis_execute_prepared_update(graal_isolatethread_t* thread, long long int stmt);
long long int wrap_h2gis_execute_prepared(graal_isolatethread_t* thread, long long int stmt);
void wrap_h2gis_close_query(graal_isolatethread_t* thread, long long int handle);
void wrap_h2gis_close_connection(graal_isolatethread_t* thread, long long int conn);
void wrap_h2gis_delete_database_and_close(graal_isolatethread_t* thread, long long int conn);
void* wrap_h2gis_fetch_all(graal_isolatethread_t* thread, long long int rs, void* sizeOut);
void* wrap_h2gis_fetch_one(graal_isolatethread_t* thread, long long int rs, void* sizeOut);
void* wrap_h2gis_fetch_batch(graal_isolatethread_t* thread, long long int rs, int batchSize, void* sizeOut);
void* wrap_h2gis_get_column_types(graal_isolatethread_t* thread, long long int stmt, void* sizeOut);
char* wrap_h2gis_get_metadata_json(graal_isolatethread_t* thread, long long int conn);
long long int wrap_h2gis_free_result_set(graal_isolatethread_t* thread, long long int rs);
void wrap_h2gis_free_result_buffer(graal_isolatethread_t* thread, void* buffer);

#ifdef __cplusplus
}
#endif

// Convenience macros to use wrapper functions with original names
#define h2gis_get_last_error        wrap_h2gis_get_last_error
#define h2gis_connect               wrap_h2gis_connect
#define h2gis_load                  wrap_h2gis_load
#define h2gis_fetch                 wrap_h2gis_fetch
#define h2gis_execute               wrap_h2gis_execute
#define h2gis_prepare               wrap_h2gis_prepare
#define h2gis_bind_double           wrap_h2gis_bind_double
#define h2gis_bind_int              wrap_h2gis_bind_int
#define h2gis_bind_long             wrap_h2gis_bind_long
#define h2gis_bind_string           wrap_h2gis_bind_string
#define h2gis_bind_blob             wrap_h2gis_bind_blob
#define h2gis_execute_prepared_update wrap_h2gis_execute_prepared_update
#define h2gis_execute_prepared      wrap_h2gis_execute_prepared
#define h2gis_close_query           wrap_h2gis_close_query
#define h2gis_close_connection      wrap_h2gis_close_connection
#define h2gis_delete_database_and_close wrap_h2gis_delete_database_and_close
#define h2gis_fetch_all             wrap_h2gis_fetch_all
#define h2gis_fetch_one             wrap_h2gis_fetch_one
#define h2gis_fetch_batch           wrap_h2gis_fetch_batch
#define h2gis_get_column_types      wrap_h2gis_get_column_types
#define h2gis_get_metadata_json     wrap_h2gis_get_metadata_json
#define h2gis_free_result_set       wrap_h2gis_free_result_set
#define h2gis_free_result_buffer    wrap_h2gis_free_result_buffer

#endif // H2GIS_WRAPPER_H_INCLUDED
