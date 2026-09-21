/**
 * @file ble_name_config.h
 * @brief BLE device name dynamic configuration with Flash persistence
 *
 * Supports runtime modification of BLE device name via CH9329 command,
 * with persistent storage in Flash. Factory reset restores default name.
 *
 * 【Compile-time guarantee】Only enabled when ENABLE_BLE is defined (KVM-GO).
 * MiniKVM firmware (built without ENABLE_BLE) has zero BLE name config code.
 */

#ifndef _BLE_NAME_CONFIG_H
#define _BLE_NAME_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ENABLE_BLE)

/**
 * @brief Initialize BLE name configuration module
 *
 * Loads saved custom name from Flash (if exists and valid).
 * Falls back to factory default name "KVM-GO" if no valid config.
 * Initializes runtime state structure @ref g_ble_name.
 *
 * This function must be called before BLE stack initialization
 * (i.e., early in Peripheral_Init()).
 *
 * 【Compile-time guarantee】Only exists when ENABLE_BLE is defined.
 */
void BLE_NameConfig_Init(void);

/**
 * @brief Set new BLE device name and persist to Flash
 *
 * Validates input name: length must be 1-20 bytes (excluding null terminator).
 * If valid, erases Flash page and writes new config record.
 * Updates runtime state, advertising data, and GAP characteristics on success.
 * Name change takes effect in next advertising cycle (via advertising restart).
 *
 * @param new_name  Pointer to null-terminated string with new name
 * @return true  if name successfully set and persisted
 * @return false if name invalid (length exceeded or null pointer) or Flash operation failed
 *
 * @note This function is not interrupt-safe; caller must ensure non-interrupt context.
 * @note 【Compile-time guarantee】Only exists when ENABLE_BLE is defined.
 */
bool BLE_NameConfig_Set(const char *new_name);

/**
 * @brief Get current active BLE device name
 *
 * Returns read-only pointer to internal name buffer.
 * Returned string is always null-terminated.
 * Do not modify the returned buffer contents.
 *
 * @return Pointer to current name (const char*)
 *
 * @note Return value may change after calling BLE_NameConfig_Set() or BLE_NameConfig_Reset().
 * @note 【Compile-time guarantee】Only exists when ENABLE_BLE is defined.
 */
const char *BLE_NameConfig_Get(void);

/**
 * @brief Reset BLE device name to factory default
 *
 * Erases Flash page containing BLE name config record.
 * Re-initializes runtime state to factory default name "KVM-GO".
 * Updates advertising data and GAP characteristics to reflect default name.
 * Restarts advertising to make change immediate.
 *
 * This operation is idempotent: multiple calls have same effect.
 *
 * @return true  if Flash erase succeeded
 * @return false if Flash erase failed (rare, only on hardware fault)
 *
 * @note This function is not interrupt-safe; caller must ensure non-interrupt context.
 * @note 【Compile-time guarantee】Only exists when ENABLE_BLE is defined.
 */
bool BLE_NameConfig_Reset(void);

/**
 * @brief Check if current name is custom (non-factory-default)
 *
 * Convenience for upper-layer logic to determine if "modified" status should be displayed.
 *
 * @return true  if current name was set via BLE_NameConfig_Set()
 * @return false if current name is factory default "KVM-GO"
 *
 * @note Depends on BLE_NameConfig_Init() having been called correctly.
 * @note 【Compile-time guarantee】Only exists when ENABLE_BLE is defined.
 */
bool BLE_NameConfig_IsCustom(void);

/**
 * @brief Runtime state structure for BLE name configuration
 */
typedef struct {
    char     name[21];        /**< Current active name (null-terminated) */
    bool     is_custom;       /**< true if using custom name, false if factory default */
} BLE_NameConfig_t;

/** Global runtime state instance */
extern BLE_NameConfig_t g_ble_name;

#else /* !ENABLE_BLE */

/**
 * @brief Empty implementation macros (MiniKVM compilation eliminates call-site code)
 *
 * When ENABLE_BLE is not defined, provides empty definitions so call code compiles
 * but produces no instructions. Avoids need for #ifdef in peripheral.c etc.
 */
#define BLE_NameConfig_Init()           do {} while(0)
#define BLE_NameConfig_Set(x)           (false)
#define BLE_NameConfig_Get()            ("KVM-GO")
#define BLE_NameConfig_Reset()          (false)
#define BLE_NameConfig_IsCustom()       (false)

typedef struct {
    char name[21];
    bool is_custom;
} BLE_NameConfig_t;

extern BLE_NameConfig_t g_ble_name;  /* Still declared to avoid compile errors, but unusable */

#endif /* ENABLE_BLE */

#ifdef __cplusplus
}
#endif

#endif /* _BLE_NAME_CONFIG_H */
