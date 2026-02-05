// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024-2026 H2GIS Team
/*******************************************************************************
 * h2gis_wrapper.cpp - Implementation of function pointer wrappers for H2GIS
 * 
 * ALL H2GIS/GraalVM operations are routed through a SINGLE dedicated worker
 * thread with a 64MB stack to avoid StackOverflowError in GraalVM Native Image.
 * 
 * Architecture:
 *   Caller Thread (QGIS worker, 8MB stack)
 *         |
 *         v
 *   [Task Queue] --> [Worker Thread with 64MB stack] --> GraalVM/H2GIS
 *         ^                       |
 *         |_______________________| (result via condition variable)
 ******************************************************************************/

#include "h2gis_wrapper.h"

#include "cpl_error.h"  // For CPLDebug
#include "cpl_conv.h"   // For CPLGetConfigOption

// ============================================================================
// Platform abstraction layer
// ============================================================================

#if defined(_WIN32) || defined(_WIN64)
#define H2GIS_PLATFORM_WINDOWS 1
#include <windows.h>
#include <io.h>
typedef HMODULE h2gis_lib_handle_t;
typedef HANDLE h2gis_thread_t;
#define H2GIS_LIB_EXT ".dll"
#elif defined(__APPLE__) && defined(__MACH__)
#define H2GIS_PLATFORM_MACOS 1
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>
typedef void *h2gis_lib_handle_t;
typedef pthread_t h2gis_thread_t;
#define H2GIS_LIB_EXT ".dylib"
#else
#define H2GIS_PLATFORM_LINUX 1
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>
typedef void *h2gis_lib_handle_t;
typedef pthread_t h2gis_thread_t;
#define H2GIS_LIB_EXT ".so"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>

// Platform abstraction functions - forward declarations
static h2gis_lib_handle_t h2gis_load_library(const char *path);
static void *h2gis_get_symbol(h2gis_lib_handle_t handle, const char *name);
static void h2gis_free_library(h2gis_lib_handle_t handle);
static const char *h2gis_get_load_error(void);
static int h2gis_file_exists(const char *path);
static void h2gis_sleep_ms(unsigned int milliseconds);
static int h2gis_create_thread_with_stack(h2gis_thread_t *thread,
                                          size_t stack_size,
                                          void *(*func)(void *), void *arg);
static int h2gis_join_thread(h2gis_thread_t thread);
static const char **h2gis_get_library_fallback_paths(void);

// ============================================================================
// Platform abstraction implementations
// ============================================================================

#if defined(H2GIS_PLATFORM_WINDOWS)

static h2gis_lib_handle_t h2gis_load_library(const char *path)
{
    return LoadLibraryA(path);
}

static void *h2gis_get_symbol(h2gis_lib_handle_t handle, const char *name)
{
    return (void *)GetProcAddress(handle, name);
}

static void h2gis_free_library(h2gis_lib_handle_t handle)
{
    if (handle)
        FreeLibrary(handle);
}

static const char *h2gis_get_load_error(void)
{
    static char buf[256];
    DWORD err = GetLastError();
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf,
                   sizeof(buf), NULL);
    return buf;
}

static int h2gis_file_exists(const char *path)
{
    return _access(path, 0) == 0;
}

static void h2gis_sleep_ms(unsigned int milliseconds)
{
    Sleep(milliseconds);
}

static int h2gis_create_thread_with_stack(h2gis_thread_t *thread,
                                          size_t stack_size,
                                          void *(*func)(void *), void *arg)
{
    // Windows CreateThread with custom stack size
    // Note: CreateThread expects LPTHREAD_START_ROUTINE which is DWORD (*)(LPVOID)
    // We cast and hope the calling convention is compatible (it usually is on x64)
    *thread = CreateThread(NULL, stack_size, (LPTHREAD_START_ROUTINE)func, arg,
                           0, NULL);
    return (*thread == NULL) ? -1 : 0;
}

static int h2gis_join_thread(h2gis_thread_t thread)
{
    if (thread == NULL)
        return -1;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

static const char **h2gis_get_library_fallback_paths(void)
{
    static const char *paths[] = {
        // Relative to executable
        "h2gis.dll",
        // Python h2gis package paths (common Windows locations)
        NULL  // Sentinel
    };
    return paths;
}

#else  // Unix-like (Linux and macOS)

static h2gis_lib_handle_t h2gis_load_library(const char *path)
{
    return dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
}

static void *h2gis_get_symbol(h2gis_lib_handle_t handle, const char *name)
{
    return dlsym(handle, name);
}

static void h2gis_free_library(h2gis_lib_handle_t handle)
{
    if (handle)
        dlclose(handle);
}

static const char *h2gis_get_load_error(void)
{
    return dlerror();
}

static int h2gis_file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static void h2gis_sleep_ms(unsigned int milliseconds)
{
    usleep(milliseconds * 1000);
}

static int h2gis_create_thread_with_stack(h2gis_thread_t *thread,
                                          size_t stack_size,
                                          void *(*func)(void *), void *arg)
{
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
        return -1;
    if (pthread_attr_setstacksize(&attr, stack_size) != 0)
    {
        pthread_attr_destroy(&attr);
        return -1;
    }
    int ret = pthread_create(thread, &attr, func, NULL);
    pthread_attr_destroy(&attr);
    return ret;
}

static int h2gis_join_thread(h2gis_thread_t thread)
{
    return pthread_join(thread, NULL);
}

#if defined(H2GIS_PLATFORM_MACOS)

static const char **h2gis_get_library_fallback_paths(void)
{
    static const char *paths[] = {
        // Python h2gis package path (typical venv location)
        "libh2gis.dylib",
        // Homebrew locations
        "/usr/local/lib/libh2gis.dylib",
        "/opt/homebrew/lib/libh2gis.dylib",
        NULL  // Sentinel
    };
    return paths;
}

#else  // Linux

static const char **h2gis_get_library_fallback_paths(void)
{
    static const char *paths[] = {
        // System library paths
        "/usr/lib/libh2gis.so",
        "/usr/local/lib/libh2gis.so",
        // Python h2gis package (site-packages)
        NULL  // Sentinel
    };
    return paths;
}

#endif  // H2GIS_PLATFORM_MACOS

#endif  // H2GIS_PLATFORM_WINDOWS

// Debug logging routed to CPLDebug
#define debug_log(fmt, ...) CPLDebug("H2GIS_WRAPPER", fmt, ##__VA_ARGS__)

// ============================================================================
// Function pointer types
// ============================================================================

typedef char *(*fn_h2gis_get_last_error)(graal_isolatethread_t *);
typedef long long (*fn_h2gis_connect)(graal_isolatethread_t *, char *, char *,
                                      char *);
typedef long long (*fn_h2gis_load)(graal_isolatethread_t *, long long);
typedef long long (*fn_h2gis_fetch)(graal_isolatethread_t *, long long, char *);
typedef int (*fn_h2gis_execute)(graal_isolatethread_t *, long long, char *);
typedef long long (*fn_h2gis_prepare)(graal_isolatethread_t *, long long,
                                      char *);
typedef void (*fn_h2gis_bind_double)(graal_isolatethread_t *, long long, int,
                                     double);
typedef void (*fn_h2gis_bind_int)(graal_isolatethread_t *, long long, int, int);
typedef void (*fn_h2gis_bind_long)(graal_isolatethread_t *, long long, int,
                                   long long);
typedef void (*fn_h2gis_bind_string)(graal_isolatethread_t *, long long, int,
                                     char *);
typedef void (*fn_h2gis_bind_blob)(graal_isolatethread_t *, long long, int,
                                   char *, int);
typedef int (*fn_h2gis_execute_prepared_update)(graal_isolatethread_t *,
                                                long long);
typedef long long (*fn_h2gis_execute_prepared)(graal_isolatethread_t *,
                                               long long);
typedef void (*fn_h2gis_close_query)(graal_isolatethread_t *, long long);
typedef void (*fn_h2gis_close_connection)(graal_isolatethread_t *, long long);
typedef void (*fn_h2gis_delete_database_and_close)(graal_isolatethread_t *,
                                                   long long);
typedef void *(*fn_h2gis_fetch_all)(graal_isolatethread_t *, long long, void *);
typedef void *(*fn_h2gis_fetch_one)(graal_isolatethread_t *, long long, void *);
typedef void *(*fn_h2gis_fetch_batch)(graal_isolatethread_t *, long long, int,
                                      void *);
typedef void *(*fn_h2gis_get_column_types)(graal_isolatethread_t *, long long,
                                           void *);
typedef char *(*fn_h2gis_get_metadata_json)(graal_isolatethread_t *, long long);
typedef long long (*fn_h2gis_free_result_set)(graal_isolatethread_t *,
                                              long long);
typedef void (*fn_h2gis_free_result_buffer)(graal_isolatethread_t *, void *);

// GraalVM function types
typedef int (*fn_graal_create_isolate)(graal_create_isolate_params_t *,
                                       graal_isolate_t **,
                                       graal_isolatethread_t **);
typedef graal_isolatethread_t *(*fn_graal_get_current_thread)(
    graal_isolate_t *);
typedef int (*fn_graal_attach_thread)(graal_isolate_t *,
                                      graal_isolatethread_t **);
typedef int (*fn_graal_detach_thread)(graal_isolatethread_t *);

// ============================================================================
// Global state
// ============================================================================

static std::mutex g_init_mutex;
static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_shutdown{false};
static h2gis_lib_handle_t g_h2gis_handle = nullptr;
static graal_isolate_t *g_isolate = nullptr;
static graal_isolatethread_t *g_worker_thread =
    nullptr;  // Thread handle for the worker

// Worker thread state
static h2gis_thread_t g_worker_thread_handle;
static std::mutex g_queue_mutex;
static std::condition_variable g_queue_cv;
static std::queue<std::function<void()>> g_task_queue;

// Reference counting for proper shutdown
static std::atomic<int> g_refcount{0};

// Function pointers
static fn_h2gis_get_last_error fp_h2gis_get_last_error = nullptr;
static fn_h2gis_connect fp_h2gis_connect = nullptr;
static fn_h2gis_load fp_h2gis_load = nullptr;
static fn_h2gis_fetch fp_h2gis_fetch = nullptr;
static fn_h2gis_execute fp_h2gis_execute = nullptr;
static fn_h2gis_prepare fp_h2gis_prepare = nullptr;
static fn_h2gis_bind_double fp_h2gis_bind_double = nullptr;
static fn_h2gis_bind_int fp_h2gis_bind_int = nullptr;
static fn_h2gis_bind_long fp_h2gis_bind_long = nullptr;
static fn_h2gis_bind_string fp_h2gis_bind_string = nullptr;
static fn_h2gis_bind_blob fp_h2gis_bind_blob = nullptr;
static fn_h2gis_execute_prepared_update fp_h2gis_execute_prepared_update =
    nullptr;
static fn_h2gis_execute_prepared fp_h2gis_execute_prepared = nullptr;
static fn_h2gis_close_query fp_h2gis_close_query = nullptr;
static fn_h2gis_close_connection fp_h2gis_close_connection = nullptr;
static fn_h2gis_delete_database_and_close fp_h2gis_delete_database_and_close =
    nullptr;
static fn_h2gis_fetch_all fp_h2gis_fetch_all = nullptr;
static fn_h2gis_fetch_one fp_h2gis_fetch_one = nullptr;
static fn_h2gis_fetch_batch fp_h2gis_fetch_batch = nullptr;
static fn_h2gis_get_column_types fp_h2gis_get_column_types = nullptr;
static fn_h2gis_get_metadata_json fp_h2gis_get_metadata_json = nullptr;
static fn_h2gis_free_result_set fp_h2gis_free_result_set = nullptr;
static fn_h2gis_free_result_buffer fp_h2gis_free_result_buffer = nullptr;

static fn_graal_create_isolate fp_graal_create_isolate = nullptr;
static fn_graal_get_current_thread fp_graal_get_current_thread = nullptr;
static fn_graal_attach_thread fp_graal_attach_thread = nullptr;
static fn_graal_detach_thread fp_graal_detach_thread = nullptr;

// ============================================================================
// Task execution helper - runs a task on the worker thread and waits
// ============================================================================

template <typename Func> auto execute_on_worker(Func &&func) -> decltype(func())
{
    using ReturnType = decltype(func());

    std::promise<ReturnType> promise;
    std::future<ReturnType> future = promise.get_future();

    {
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        g_task_queue.push(
            [&promise, &func]()
            {
                try
                {
                    if constexpr (std::is_void_v<ReturnType>)
                    {
                        func();
                        promise.set_value();
                    }
                    else
                    {
                        promise.set_value(func());
                    }
                }
                catch (...)
                {
                    promise.set_exception(std::current_exception());
                }
            });
    }
    g_queue_cv.notify_one();

    return future.get();
}

// ============================================================================
// Worker thread function - runs with 64MB stack
// ============================================================================

static void *worker_thread_func(void *arg)
{
    debug_log("worker_thread_func: Starting worker thread with 64MB stack");

    // Load library and create isolate HERE (on the large-stack thread)
    const char *lib_path = CPLGetConfigOption("H2GIS_NATIVE_LIB", nullptr);
    if (!lib_path)
    {
        lib_path = CPLGetConfigOption("H2GIS_LIBRARY", nullptr);
    }

    // Get platform-specific fallback paths
    const char **fallbacks = h2gis_get_library_fallback_paths();

    if (lib_path)
    {
        debug_log("worker_thread_func: Loading explicit library path: %s",
                  lib_path);
        g_h2gis_handle = h2gis_load_library(lib_path);
    }
    else
    {
        for (int i = 0; fallbacks[i]; i++)
        {
            if (h2gis_file_exists(fallbacks[i]))
            {
                debug_log("worker_thread_func: Found library at %s",
                          fallbacks[i]);
                g_h2gis_handle = h2gis_load_library(fallbacks[i]);
                if (g_h2gis_handle)
                {
                    lib_path = fallbacks[i];
                    break;
                }
            }
        }
    }

    if (!g_h2gis_handle)
    {
        debug_log("worker_thread_func: Library load failed: %s",
                  h2gis_get_load_error());
        return (void *)-1;
    }

    debug_log("worker_thread_func: Library loaded, resolving symbols...");

    // Resolve GraalVM functions
    fp_graal_create_isolate =
        (fn_graal_create_isolate)h2gis_get_symbol(g_h2gis_handle,
                                                   "graal_create_isolate");
    fp_graal_get_current_thread = (fn_graal_get_current_thread)h2gis_get_symbol(
        g_h2gis_handle, "graal_get_current_thread");
    fp_graal_attach_thread =
        (fn_graal_attach_thread)h2gis_get_symbol(g_h2gis_handle,
                                                  "graal_attach_thread");
    fp_graal_detach_thread =
        (fn_graal_detach_thread)h2gis_get_symbol(g_h2gis_handle,
                                                  "graal_detach_thread");

    if (!fp_graal_create_isolate)
    {
        debug_log("worker_thread_func: Failed to resolve graal_create_isolate");
        h2gis_free_library(g_h2gis_handle);
        g_h2gis_handle = nullptr;
        return (void *)-1;
    }

    // Resolve H2GIS functions
    fp_h2gis_get_last_error =
        (fn_h2gis_get_last_error)h2gis_get_symbol(g_h2gis_handle,
                                                   "h2gis_get_last_error");
    fp_h2gis_connect =
        (fn_h2gis_connect)h2gis_get_symbol(g_h2gis_handle, "h2gis_connect");
    fp_h2gis_load =
        (fn_h2gis_load)h2gis_get_symbol(g_h2gis_handle, "h2gis_load");
    fp_h2gis_fetch =
        (fn_h2gis_fetch)h2gis_get_symbol(g_h2gis_handle, "h2gis_fetch");
    fp_h2gis_execute =
        (fn_h2gis_execute)h2gis_get_symbol(g_h2gis_handle, "h2gis_execute");
    fp_h2gis_prepare =
        (fn_h2gis_prepare)h2gis_get_symbol(g_h2gis_handle, "h2gis_prepare");
    fp_h2gis_bind_double =
        (fn_h2gis_bind_double)h2gis_get_symbol(g_h2gis_handle,
                                                "h2gis_bind_double");
    fp_h2gis_bind_int =
        (fn_h2gis_bind_int)h2gis_get_symbol(g_h2gis_handle, "h2gis_bind_int");
    fp_h2gis_bind_long =
        (fn_h2gis_bind_long)h2gis_get_symbol(g_h2gis_handle, "h2gis_bind_long");
    fp_h2gis_bind_string =
        (fn_h2gis_bind_string)h2gis_get_symbol(g_h2gis_handle,
                                                "h2gis_bind_string");
    fp_h2gis_bind_blob =
        (fn_h2gis_bind_blob)h2gis_get_symbol(g_h2gis_handle, "h2gis_bind_blob");
    fp_h2gis_execute_prepared_update =
        (fn_h2gis_execute_prepared_update)h2gis_get_symbol(
            g_h2gis_handle, "h2gis_execute_prepared_update");
    fp_h2gis_execute_prepared = (fn_h2gis_execute_prepared)h2gis_get_symbol(
        g_h2gis_handle, "h2gis_execute_prepared");
    fp_h2gis_close_query =
        (fn_h2gis_close_query)h2gis_get_symbol(g_h2gis_handle,
                                                "h2gis_close_query");
    fp_h2gis_close_connection = (fn_h2gis_close_connection)h2gis_get_symbol(
        g_h2gis_handle, "h2gis_close_connection");
    fp_h2gis_delete_database_and_close =
        (fn_h2gis_delete_database_and_close)h2gis_get_symbol(
            g_h2gis_handle, "h2gis_delete_database_and_close");
    fp_h2gis_fetch_all =
        (fn_h2gis_fetch_all)h2gis_get_symbol(g_h2gis_handle, "h2gis_fetch_all");
    fp_h2gis_fetch_one =
        (fn_h2gis_fetch_one)h2gis_get_symbol(g_h2gis_handle, "h2gis_fetch_one");
    fp_h2gis_fetch_batch =
        (fn_h2gis_fetch_batch)h2gis_get_symbol(g_h2gis_handle,
                                                "h2gis_fetch_batch");
    fp_h2gis_get_column_types = (fn_h2gis_get_column_types)h2gis_get_symbol(
        g_h2gis_handle, "h2gis_get_column_types");
    fp_h2gis_get_metadata_json = (fn_h2gis_get_metadata_json)h2gis_get_symbol(
        g_h2gis_handle, "h2gis_get_metadata_json");
    fp_h2gis_free_result_set = (fn_h2gis_free_result_set)h2gis_get_symbol(
        g_h2gis_handle, "h2gis_free_result_set");
    fp_h2gis_free_result_buffer = (fn_h2gis_free_result_buffer)h2gis_get_symbol(
        g_h2gis_handle, "h2gis_free_result_buffer");

    if (!fp_h2gis_connect || !fp_h2gis_execute || !fp_h2gis_prepare)
    {
        debug_log(
            "worker_thread_func: Failed to resolve required H2GIS functions");
        h2gis_free_library(g_h2gis_handle);
        g_h2gis_handle = nullptr;
        return (void *)-1;
    }

    debug_log("worker_thread_func: All symbols resolved, creating isolate...");

    // Create GraalVM isolate ON THIS THREAD (with 64MB stack)
    graal_create_isolate_params_t params = {};
    params.version = 4;
    int rc = fp_graal_create_isolate(&params, &g_isolate, &g_worker_thread);
    if (rc != 0)
    {
        debug_log("worker_thread_func: graal_create_isolate failed: %d", rc);
        h2gis_free_library(g_h2gis_handle);
        g_h2gis_handle = nullptr;
        return (void *)-1;
    }

    debug_log("worker_thread_func: Isolate created! isolate=%p, thread=%p",
              (void *)g_isolate, (void *)g_worker_thread);

    // Signal that initialization is complete
    g_initialized.store(true);

    // Main task processing loop
    debug_log("worker_thread_func: Entering task loop...");
    while (!g_shutdown.load())
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(g_queue_mutex);
            g_queue_cv.wait(
                lock,
                [] { return !g_task_queue.empty() || g_shutdown.load(); });

            if (g_shutdown.load() && g_task_queue.empty())
            {
                break;
            }

            if (!g_task_queue.empty())
            {
                task = std::move(g_task_queue.front());
                g_task_queue.pop();
            }
        }

        if (task)
        {
            task();
        }
    }

    debug_log("worker_thread_func: Shutting down...");

    // Cleanup
    if (g_worker_thread && fp_graal_detach_thread)
    {
        fp_graal_detach_thread(g_worker_thread);
    }

    return nullptr;
}

// ============================================================================
// Public initialization
// ============================================================================

extern "C" int h2gis_wrapper_init(void)
{
    if (g_initialized.load())
    {
        return 0;
    }

    debug_log("h2gis_wrapper_init: Starting...");

    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_initialized.load())
    {
        return 0;
    }

    debug_log("h2gis_wrapper_init: Creating worker thread with 64MB stack...");

    size_t stack_size = 64 * 1024 * 1024;  // 64 MB
    if (h2gis_create_thread_with_stack(&g_worker_thread_handle, stack_size,
                                        worker_thread_func, nullptr) != 0)
    {
        debug_log("h2gis_wrapper_init: Failed to create worker thread");
        return -1;
    }

    // Wait for initialization to complete (with timeout)
    int wait_count = 0;
    while (!g_initialized.load() && wait_count < 100)
    {  // 10 second timeout
        h2gis_sleep_ms(100);
        wait_count++;
    }

    if (!g_initialized.load())
    {
        debug_log("h2gis_wrapper_init: Timeout waiting for worker thread init");
        g_shutdown.store(true);
        g_queue_cv.notify_all();
        h2gis_join_thread(g_worker_thread_handle);
        return -1;
    }

    // Register atexit handler to ensure clean shutdown when process exits
    /*
    static bool atexit_registered = false;
    if (!atexit_registered)
    {
        atexit(h2gis_wrapper_shutdown);
        atexit_registered = true;
        debug_log("h2gis_wrapper_init: atexit handler registered");
    }
    */
    // NOTE: We now use GDAL's pfnUnloadDriver mechanism (OGRH2GISDriverUnload)
    // to trigger shutdown. This is safer than atexit which may run too late.

    debug_log("h2gis_wrapper_init: Success!");
    return 0;
}

extern "C" int h2gis_wrapper_is_initialized(void)
{
    return g_initialized.load() ? 1 : 0;
}

extern "C" void h2gis_wrapper_add_ref(void)
{
    g_refcount.fetch_add(1);
    debug_log("h2gis_wrapper_add_ref: refcount=%d", g_refcount.load());
}

extern "C" void h2gis_wrapper_release(void)
{
    int prev = g_refcount.fetch_sub(1);
    debug_log("h2gis_wrapper_release: refcount=%d (was %d)", g_refcount.load(),
              prev);
    if (prev == 1)
    {
        // Last reference, shutdown worker thread
        h2gis_wrapper_shutdown();
    }
}

extern "C" void h2gis_wrapper_shutdown(void)
{
    if (!g_initialized.load())
    {
        return;
    }

    debug_log("h2gis_wrapper_shutdown: Signaling shutdown...");

    g_shutdown.store(true);
    g_queue_cv.notify_all();

    debug_log("h2gis_wrapper_shutdown: Waiting for worker thread to exit...");
    h2gis_join_thread(g_worker_thread_handle);

    if (g_h2gis_handle)
    {
        h2gis_free_library(g_h2gis_handle);
        g_h2gis_handle = nullptr;
    }

    g_initialized.store(false);
    g_isolate = nullptr;
    g_worker_thread = nullptr;

    debug_log("h2gis_wrapper_shutdown: Complete");
}

extern "C" graal_isolate_t *h2gis_wrapper_get_isolate(void)
{
    if (!g_initialized.load() && h2gis_wrapper_init() != 0)
    {
        return nullptr;
    }
    return g_isolate;
}

extern "C" graal_isolatethread_t *h2gis_wrapper_get_thread(void)
{
    if (!g_initialized.load() && h2gis_wrapper_init() != 0)
    {
        return nullptr;
    }
    // Always return the worker thread - all operations go through it
    return g_worker_thread;
}

// ============================================================================
// Wrapper functions - ALL operations are routed through the worker thread
// ============================================================================

extern "C" char *wrap_h2gis_get_last_error(graal_isolatethread_t *thread)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_get_last_error)
        return nullptr;
    return execute_on_worker(
        [&]() { return fp_h2gis_get_last_error(g_worker_thread); });
}

extern "C" long long int wrap_h2gis_connect(graal_isolatethread_t *thread,
                                            char *path, char *user, char *pass)
{
    if (!h2gis_wrapper_is_initialized() && h2gis_wrapper_init() != 0)
        return -1;
    if (!fp_h2gis_connect)
        return -1;
    debug_log("wrap_h2gis_connect: Connecting to %s", path);
    long long conn = execute_on_worker(
        [&]() { return fp_h2gis_connect(g_worker_thread, path, user, pass); });
    debug_log("wrap_h2gis_connect: Result %lld", conn);
    return conn;
}

extern "C" long long int wrap_h2gis_load(graal_isolatethread_t *thread,
                                         long long int conn)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_load)
        return -1;
    debug_log("wrap_h2gis_load: Loading functions for conn %lld", conn);
    return execute_on_worker([&]()
                             { return fp_h2gis_load(g_worker_thread, conn); });
}

extern "C" long long int wrap_h2gis_fetch(graal_isolatethread_t *thread,
                                          long long int rs, char *sql)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_fetch)
        return -1;
    return execute_on_worker(
        [&]() { return fp_h2gis_fetch(g_worker_thread, rs, sql); });
}

extern "C" int wrap_h2gis_execute(graal_isolatethread_t *thread,
                                  long long int conn, char *sql)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_execute)
        return -1;
    return execute_on_worker(
        [&]() { return fp_h2gis_execute(g_worker_thread, conn, sql); });
}

extern "C" long long int wrap_h2gis_prepare(graal_isolatethread_t *thread,
                                            long long int conn, char *sql)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_prepare)
        return 0;
    debug_log("wrap_h2gis_prepare: SQL = %.100s...", sql);
    return execute_on_worker(
        [&]() { return fp_h2gis_prepare(g_worker_thread, conn, sql); });
}

extern "C" void wrap_h2gis_bind_double(graal_isolatethread_t *thread,
                                       long long int stmt, int idx, double val)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_bind_double)
        return;
    execute_on_worker(
        [&]() { fp_h2gis_bind_double(g_worker_thread, stmt, idx, val); });
}

extern "C" void wrap_h2gis_bind_int(graal_isolatethread_t *thread,
                                    long long int stmt, int idx, int val)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_bind_int)
        return;
    execute_on_worker([&]()
                      { fp_h2gis_bind_int(g_worker_thread, stmt, idx, val); });
}

extern "C" void wrap_h2gis_bind_long(graal_isolatethread_t *thread,
                                     long long int stmt, int idx,
                                     long long int val)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_bind_long)
        return;
    execute_on_worker([&]()
                      { fp_h2gis_bind_long(g_worker_thread, stmt, idx, val); });
}

extern "C" void wrap_h2gis_bind_string(graal_isolatethread_t *thread,
                                       long long int stmt, int idx, char *val)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_bind_string)
        return;
    execute_on_worker(
        [&]() { fp_h2gis_bind_string(g_worker_thread, stmt, idx, val); });
}

extern "C" void wrap_h2gis_bind_blob(graal_isolatethread_t *thread,
                                     long long int stmt, int idx, char *data,
                                     int len)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_bind_blob)
        return;
    execute_on_worker(
        [&]() { fp_h2gis_bind_blob(g_worker_thread, stmt, idx, data, len); });
}

extern "C" int wrap_h2gis_execute_prepared_update(graal_isolatethread_t *thread,
                                                  long long int stmt)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_execute_prepared_update)
        return -1;
    return execute_on_worker(
        [&]()
        { return fp_h2gis_execute_prepared_update(g_worker_thread, stmt); });
}

extern "C" long long int
wrap_h2gis_execute_prepared(graal_isolatethread_t *thread, long long int stmt)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_execute_prepared)
        return 0;
    return execute_on_worker(
        [&]() { return fp_h2gis_execute_prepared(g_worker_thread, stmt); });
}

extern "C" void wrap_h2gis_close_query(graal_isolatethread_t *thread,
                                       long long int handle)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_close_query || handle == 0)
        return;
    execute_on_worker([&]() { fp_h2gis_close_query(g_worker_thread, handle); });
}

extern "C" void wrap_h2gis_close_connection(graal_isolatethread_t *thread,
                                            long long int conn)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_close_connection ||
        conn < 0)
        return;
    execute_on_worker([&]()
                      { fp_h2gis_close_connection(g_worker_thread, conn); });
}

extern "C" void
wrap_h2gis_delete_database_and_close(graal_isolatethread_t *thread,
                                     long long int conn)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_delete_database_and_close)
        return;
    execute_on_worker(
        [&]() { fp_h2gis_delete_database_and_close(g_worker_thread, conn); });
}

extern "C" void *wrap_h2gis_fetch_all(graal_isolatethread_t *thread,
                                      long long int rs, void *sizeOut)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_fetch_all)
        return nullptr;
    return execute_on_worker(
        [&]() { return fp_h2gis_fetch_all(g_worker_thread, rs, sizeOut); });
}

extern "C" void *wrap_h2gis_fetch_one(graal_isolatethread_t *thread,
                                      long long int rs, void *sizeOut)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_fetch_one)
        return nullptr;
    return execute_on_worker(
        [&]() { return fp_h2gis_fetch_one(g_worker_thread, rs, sizeOut); });
}

extern "C" void *wrap_h2gis_fetch_batch(graal_isolatethread_t *thread,
                                        long long int rs, int batchSize,
                                        void *sizeOut)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_fetch_batch)
        return nullptr;
    return execute_on_worker(
        [&]() {
            return fp_h2gis_fetch_batch(g_worker_thread, rs, batchSize,
                                        sizeOut);
        });
}

extern "C" void *wrap_h2gis_get_column_types(graal_isolatethread_t *thread,
                                             long long int stmt, void *sizeOut)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_get_column_types)
        return nullptr;
    return execute_on_worker(
        [&]()
        { return fp_h2gis_get_column_types(g_worker_thread, stmt, sizeOut); });
}

extern "C" char *wrap_h2gis_get_metadata_json(graal_isolatethread_t *thread,
                                              long long int conn)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_get_metadata_json)
        return nullptr;
    return execute_on_worker(
        [&]() { return fp_h2gis_get_metadata_json(g_worker_thread, conn); });
}

extern "C" long long int
wrap_h2gis_free_result_set(graal_isolatethread_t *thread, long long int rs)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_free_result_set)
        return -1;
    return execute_on_worker(
        [&]() { return fp_h2gis_free_result_set(g_worker_thread, rs); });
}

extern "C" void wrap_h2gis_free_result_buffer(graal_isolatethread_t *thread,
                                              void *buffer)
{
    if (!h2gis_wrapper_is_initialized() || !fp_h2gis_free_result_buffer ||
        buffer == nullptr)
        return;
    execute_on_worker(
        [&]() { fp_h2gis_free_result_buffer(g_worker_thread, buffer); });
}
