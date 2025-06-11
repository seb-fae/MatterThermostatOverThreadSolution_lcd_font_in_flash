/*******************************************************************************
 * @file
 * @brief CMSIS RTOS2 adaptation for running OpenThread in RTOS
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#include <assert.h>
#include <stdio.h>

#include <openthread-core-config.h>
#include <openthread-system.h>
#include <openthread/tasklet.h>
#include <openthread/platform/time.h>
#include <openthread/platform/toolchain.h>

#include <mbedtls/platform.h>

#include "cmsis_os2.h"
#include "platform-efr32.h"
#include "sl_cmsis_os2_common.h"
#include "sl_component_catalog.h"
#include "sl_openthread_rtos_config.h"
#include "sl_ot_init.h"
#include "sl_ot_rtos_adaptation.h"

#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
#include "sleep.h"
#endif

// Structure defining task information
typedef struct sl_ot_rtos_thread_t
{
    osThreadId_t                id;              // Task ID
    osSemaphoreId_t             semaphore;       // Task wake semaphore
    volatile sl_ot_rtos_event_t events;          // Task specific flags, to notify about the pending event
    uint64_t                    active_duration; // Tracking the active duration of the stacks.
} sl_ot_rtos_thread_t;

/* OT main stack task specific settings */
static sl_ot_rtos_thread_t sli_ot_stack_task_info;
__ALIGNED(8) static uint8_t sli_ot_stack_task_mem[SL_OPENTHREAD_STACK_TASK_MEM_SIZE];
__ALIGNED(4) static uint8_t sli_ot_stack_task_cb[osThreadCbSize];
static void sli_ot_stack_task(void *aContext);

static const osThreadAttr_t sli_ot_stack_task_attr = {
    .name       = "OT Stack",
    .attr_bits  = 0u,
    .stack_size = SL_OPENTHREAD_STACK_TASK_MEM_SIZE,
    .stack_mem  = sli_ot_stack_task_mem,
    .cb_mem     = sli_ot_stack_task_cb,
    .cb_size    = osThreadCbSize,
    .priority   = (osPriority_t)SL_OPENTHREAD_RTOS_STACK_TASK_PRIORITY,
};

static const osSemaphoreAttr_t sli_ot_stack_semaphore_attr = {.name = "OT Stack Semaphore"};

/* OT app task specific settings */
#if SL_OPENTHREAD_ENABLE_APP_TASK
static sl_ot_rtos_thread_t sli_ot_app_task_info;
__ALIGNED(8) static uint8_t sli_ot_app_task_mem[SL_OPENTHREAD_APP_TASK_MEM_SIZE];
__ALIGNED(4) static uint8_t sli_ot_app_task_cb[osThreadCbSize];
static void sli_ot_app_task(void *aContext);

static const osThreadAttr_t sli_ot_app_task_attr = {
    .name       = "OT App",
    .attr_bits  = 0u,
    .stack_size = SL_OPENTHREAD_APP_TASK_MEM_SIZE,
    .stack_mem  = sli_ot_app_task_mem,
    .cb_mem     = sli_ot_app_task_cb,
    .cb_size    = osThreadCbSize,
    .priority   = (osPriority_t)SL_OPENTHREAD_RTOS_APP_TASK_PRIORITY,
};

static const osSemaphoreAttr_t sli_ot_app_semaphore_attr = {.name = "OT App Semaphore"};
#endif

/* OT Serial Task specific settings */
#if SL_OPENTHREAD_ENABLE_SERIAL_TASK
static sl_ot_rtos_thread_t sli_ot_serial_task_info;
__ALIGNED(8) static uint8_t sli_ot_serial_task_mem[SL_OPENTHREAD_SERIAL_TASK_MEM_SIZE];
__ALIGNED(4) static uint8_t sli_ot_serial_task_cb[osThreadCbSize];
static void sli_ot_serial_task(void *aContext);

static const osThreadAttr_t sli_ot_serial_task_attr = {
    .name       = "OT Serial",
    .attr_bits  = 0u,
    .stack_size = SL_OPENTHREAD_SERIAL_TASK_MEM_SIZE,
    .stack_mem  = sli_ot_serial_task_mem,
    .cb_mem     = sli_ot_serial_task_cb,
    .cb_size    = osThreadCbSize,
    .priority   = (osPriority_t)SL_OPENTHREAD_RTOS_SERIAL_TASK_PRIORITY,
};

static const osSemaphoreAttr_t sli_ot_serial_semaphore_attr = {.name = "OT Serial Semaphore"};
#endif // SL_OPENTHREAD_ENABLE_SERIAL_TASK

osMutexId_t                sli_ot_stack_mutex;
static const osMutexAttr_t sli_stack_mutex_attributes = {
    .name      = "OT Stack Mutex",
    .attr_bits = osMutexRecursive | osMutexPrioInherit,
};

otInstance *otGetInstance(void);

// Create the main task mutex. This mutex will be used to grant access to stack.
static void sl_ot_rtos_create_stack_mutex(void)
{
    sli_ot_stack_mutex = osMutexNew(&sli_stack_mutex_attributes);
    EFM_ASSERT(sli_ot_stack_mutex != NULL);
}

// Helper function to create a task and a semaphore that can be used to wake the task
static void sl_ot_rtos_stack_create(sl_ot_rtos_thread_t     *task_info,
                                    const osThreadFunc_t     task_handler,
                                    const osThreadAttr_t    *task_attributes,
                                    const osSemaphoreAttr_t *semaphore_attributes)
{
    static const uint32_t MAX_SEMAPHORE_COUNT     = 1;
    static const uint32_t INITIAL_SEMAPHORE_COUNT = 0;

    task_info->events    = 0;
    task_info->semaphore = osSemaphoreNew(MAX_SEMAPHORE_COUNT, INITIAL_SEMAPHORE_COUNT, semaphore_attributes);
    task_info->id        = osThreadNew(*task_handler, NULL, task_attributes);
}

// Wait loop for tasks. We acquire the task semaphore and acquire the semaphore and external events can
// Wake the task by setting the event flag and releasing the task semaphore.
static sl_ot_rtos_event_t sl_ot_rtos_wait_for_event(sl_ot_rtos_thread_t *task_info, sl_ot_rtos_event_t event)
{
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
    sl_ot_sleep_update();
#endif
    sl_ot_rtos_event_t accessible_events = task_info->events & event;
    while (accessible_events == 0)
    {
        (void)osSemaphoreAcquire(task_info->semaphore, osWaitForever);
        accessible_events = task_info->events & event;
    }
    task_info->events &= ~accessible_events;
    return accessible_events;
}

// Helper function to set the event flag for the task for notification
static void sl_ot_rtos_set_event_flag(sl_ot_rtos_thread_t *task_info, sl_ot_rtos_event_t event)
{
    task_info->events |= event;
    (void)osSemaphoreRelease(task_info->semaphore);
}

// Initialize the stack task.
void sl_ot_rtos_stack_init(void)
{
    // Create main stack task for operation..
    sl_ot_rtos_stack_create(&sli_ot_stack_task_info,
                            sli_ot_stack_task,
                            &sli_ot_stack_task_attr,
                            &sli_ot_stack_semaphore_attr);

#if SL_OPENTHREAD_ENABLE_SERIAL_TASK
    // If the Serial Task is needed, create one..
    sl_ot_rtos_stack_create(&sli_ot_serial_task_info,
                            sli_ot_serial_task,
                            &sli_ot_serial_task_attr,
                            &sli_ot_serial_semaphore_attr);
#endif

    // Create the main stack mutex. This mutex will be used to request access for stack.
    sl_ot_rtos_create_stack_mutex();

    // Handling OpenThread initialization here
    // ensures that, any functions calling mbedTLS API
    // (and subsequently OSMutexPend), are called after the kernel has started.
    sl_ot_init();
}

// If the default app task is needed, create one..
void sl_ot_rtos_app_init(void)
{
#if SL_OPENTHREAD_ENABLE_APP_TASK
    sl_ot_rtos_stack_create(&sli_ot_app_task_info, sli_ot_app_task, &sli_ot_app_task_attr, &sli_ot_app_semaphore_attr);
#endif
}

void sl_ot_rtos_set_pending_event(sl_ot_rtos_event_t event)
{
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
    sl_ot_sleep_update();
#endif

#if SL_OPENTHREAD_ENABLE_SERIAL_TASK
    if (event & SL_OT_RTOS_EVENT_SERIAL)
    {
        // If SL_OT_RTOS_EVENT_SERIAL is raised, set the event flag for the Serial Task.
        sl_ot_rtos_set_event_flag(&sli_ot_serial_task_info, event);
    }
#endif

#if SL_OPENTHREAD_ENABLE_APP_TASK
    if (event & SL_OT_RTOS_EVENT_APP)
    {
        // If SL_OT_RTOS_EVENT_APP is raised, set the event flag for app task.
        sl_ot_rtos_set_event_flag(&sli_ot_app_task_info, event);
        sl_ot_rtos_set_event_flag(&sli_ot_stack_task_info, event);
    }
#endif

    if (event & SL_OT_RTOS_EVENT_STACK)
    {
        // If SL_OT_RTOS_EVENT_STACK is raised, set the event flag for stack task.
        sl_ot_rtos_set_event_flag(&sli_ot_stack_task_info, event);
    }
}

static void sli_ot_stack_task(void *context)
{
    OT_UNUSED_VARIABLE(context);

#if defined(SL_CATALOG_OT_CRASH_HANDLER_PRESENT)
    efr32PrintResetInfo();
#endif

    // Get the OT instance for stack operation
    otInstance *instance = otGetInstance();

    while (!otSysPseudoResetWasRequested())
    {
        uint64_t timestamp = otPlatTimeGet();
        // Acquire mutex for stack access
        sl_ot_rtos_acquire_stack_mutex();

        // Process callbacks and tasklets
        otSysProcessDrivers(instance);
        otTaskletsProcess(instance);

        // Release the stack mutex
        sl_ot_rtos_release_stack_mutex();

        if (!otTaskletsArePending(instance))
        {
            sli_ot_stack_task_info.active_duration += otPlatTimeGet() - timestamp;
            // If tasklets are not pending, wait for stack event..
            (void)sl_ot_rtos_wait_for_event(&sli_ot_stack_task_info, SL_OT_RTOS_EVENT_STACK);
        }
    }

    // If Reset was requested, finalize OT and terminate the thread..
    otInstanceFinalize(instance);
    osThreadTerminate(sli_ot_stack_task_info.id);
}

#if SL_OPENTHREAD_ENABLE_APP_TASK
static void sli_ot_app_task(void *context)
{
    OT_UNUSED_VARIABLE(context);

    while (!otSysPseudoResetWasRequested())
    {
        uint64_t timestamp = otPlatTimeGet();
        // Give a tick to application..
        sl_ot_rtos_application_tick();
        sli_ot_app_task_info.active_duration += otPlatTimeGet() - timestamp;
        // Wait for the app event..
        (void)sl_ot_rtos_wait_for_event(&sli_ot_app_task_info, SL_OT_RTOS_EVENT_APP);
    }
    // If reset is requested terminate the app task..
    osThreadTerminate(sli_ot_app_task_info.id);
}
#endif // SL_OPENTHREAD_ENABLE_APP_TASK

#if SL_OPENTHREAD_ENABLE_SERIAL_TASK
static void sli_ot_serial_task(void *aContext)
{
    (void)aContext;

    while (1)
    {
        uint64_t timestamp = otPlatTimeGet();

        // Process serial events. We acquire stack mutex here because, we will be accessing
        // the stack to provide serial callbacks. As these are platform level callbacks, it
        // must be in stack context.
        sl_ot_rtos_acquire_stack_mutex();
        efr32SerialProcess();
        sl_ot_rtos_release_stack_mutex();

        sli_ot_serial_task_info.active_duration += otPlatTimeGet() - timestamp;
        // Wait for the serial event..
        (void)sl_ot_rtos_wait_for_event(&sli_ot_serial_task_info, SL_OT_RTOS_EVENT_SERIAL);
    }
}
#endif

void otTaskletsSignalPending(otInstance *aInstance)
{
    (void)aInstance;
    sl_ot_rtos_set_pending_event(SL_OT_RTOS_EVENT_STACK);
}

void otSysEventSignalPending(void)
{
    sl_ot_rtos_set_pending_event(SL_OT_RTOS_EVENT_STACK);
}

bool sl_ot_rtos_task_can_access_pal(void)
{
    // ToDo: Address the PAL lock by changing the order of stack initialization
    return true;
}

OT_TOOL_WEAK void sl_ot_rtos_application_tick(void)
{
}

OT_TOOL_WEAK void sl_ot_rtos_acquire_stack_mutex(void)
{
    osStatus_t error = osMutexAcquire(sli_ot_stack_mutex, osWaitForever);
    OT_UNUSED_VARIABLE(error);
}

OT_TOOL_WEAK void sl_ot_rtos_release_stack_mutex(void)
{
    osStatus_t error = osMutexRelease(sli_ot_stack_mutex);
    OT_UNUSED_VARIABLE(error);
}

OT_TOOL_WEAK bool sl_ot_rtos_try_acquire_stack_mutex(void)
{
    osStatus_t error = osMutexAcquire(sli_ot_stack_mutex, 0);
    return (error == osOK);
}
