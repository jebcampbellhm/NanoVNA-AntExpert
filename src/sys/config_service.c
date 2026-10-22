/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Originally written using elements from Dmitry (DiSlord) dislordlive@gmail.com
 * Originally written using elements from TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "sys/config_service.h"
#include "sys/event_bus.h"
#include "processing/antenna_cal.h"

#include <stdbool.h>
#include <string.h>
#ifdef NANOVNA_F303
#pragma GCC optimize("Os")
#endif

_Static_assert(sizeof(properties_t) <= SAVE_PROP_CONFIG_SIZE, "Calibration exceeds its flash slot");
_Static_assert(sizeof(config_t) <= SAVE_CONFIG_SIZE, "Configuration exceeds its flash slot");

// Semaphore to protect flash operations from concurrent access
static semaphore_t flash_operation_semaphore;



uint16_t lastsaveid = 0;
#if SAVEAREA_MAX >= 8
#error "Increase checksum_ok type for save more cache slots"
#endif

// properties CRC check cache (max 8 slots)
static uint8_t checksum_ok = 0;
static event_bus_t* config_event_bus = NULL;

static void config_on_configuration_changed(const event_bus_message_t* message, void* user_data) {
  (void)user_data;
  if (message == NULL) {
    return;
  }
  if (message->topic != EVENT_CONFIGURATION_CHANGED) {
    return;
  }
  (void)config_save();
}

static uint32_t calibration_slot_area(int id) {
  return SAVE_PROP_CONFIG_ADDR + id * SAVE_PROP_CONFIG_SIZE;
}

static uint32_t checksum(const void* start, size_t len) {
  uint32_t* p = (uint32_t*)start;
  uint32_t value = 0;
  // align by sizeof(uint32_t)
  len = (len + sizeof(uint32_t) - 1) / sizeof(uint32_t);
  while (len-- > 0)
    value = __ROR(value, 31) + *p++;
  return value;
}

void properties_update_checksum(properties_t *p) {
  p->checksum = checksum(p, sizeof *p - sizeof p->checksum);
}

static bool finite_value(float value) {
  uint32_t bits;
  memcpy(&bits,&value,sizeof bits);
  return (bits & 0x7f800000u) != 0x7f800000u;
}

bool properties_valid(const properties_t *p) {
  bool magic_ok = p->magic == PROPERTIES_MAGIC;
#ifdef NANOVNA_F303
  magic_ok |= antenna_cal_valid(p);
#endif
  if (!magic_ok || checksum(p, sizeof *p - sizeof p->checksum) != p->checksum ||
      p->_sweep_points < 2 || p->_sweep_points > SWEEP_POINTS_MAX ||
      p->_cal_sweep_points < 2 || p->_cal_sweep_points > SWEEP_POINTS_MAX ||
      p->_frequency0 < FREQUENCY_MIN || p->_frequency1 > FREQUENCY_MAX ||
      p->_frequency0 > p->_frequency1 || p->_cal_frequency0 < FREQUENCY_MIN ||
      p->_cal_frequency1 > FREQUENCY_MAX || p->_cal_frequency0 > p->_cal_frequency1 ||
      p->_current_trace < -1 || p->_current_trace >= TRACES_MAX ||
      p->_active_marker < -1 || p->_active_marker >= MARKERS_MAX ||
      p->_previous_marker < -1 || p->_previous_marker >= MARKERS_MAX ||
      !finite_value(p->_portz) || p->_portz <= 0 ||
      !finite_value(p->_cal_load_r) || p->_cal_load_r <= 0)
    return false;
#ifdef __VNA_MEASURE_MODULE__
  if (p->_measure >= MEASURE_END) return false;
#endif
  for (unsigned i=0;i<TRACES_MAX;++i) {
    const trace_t *t=&p->_trace[i];
    if (t->type >= MAX_TRACE_TYPE || t->channel > 1 || t->smith_format >= MS_END ||
        !finite_value(t->scale) || !finite_value(t->refpos) ||
        !(t->scale > 0 && t->scale < 1e30f)) return false;
  }
  for (unsigned i=0;i<MARKERS_MAX;++i)
    if (p->_markers[i].index >= SWEEP_POINTS_MAX) return false;
  for (unsigned c=0;c<CAL_TYPE_COUNT;++c)
    for (unsigned i=0;i<p->_cal_sweep_points;++i)
      if (!finite_value(p->_cal_data[c][i][0]) || !finite_value(p->_cal_data[c][i][1])) return false;
  return true;
}

// SD CAL import stages in the existing ANT scratch area, never a saved slot.
int properties_stage_write(unsigned offset, const void *data, unsigned size) {
  if (antenna_cal_stage() || calibration_in_progress || offset % 2048 ||
      size > 2048 || !size || (size & 1) || offset >= sizeof(properties_t) ||
      size > sizeof(properties_t)-offset) return -1;
  if (chSemWaitTimeout(&flash_operation_semaphore, MS2ST(500)) != MSG_OK) return -1;
  void *dst=(void *)(uintptr_t)(ANT_CAL_SCRATCH+offset);
  flash_program_half_word_buffer(dst, (uint16_t *)data, size);
  int result=memcmp(dst,data,size) ? -1 : 0;
  chSemSignal(&flash_operation_semaphore);
  return result;
}

static int config_save_impl(void) {
  // Wait for exclusive access to flash operations with timeout to prevent blocking measurements  
  msg_t msg = MSG_OK;
  // During calibration, don't wait for semaphore as it could delay critical measurements
  if (calibration_in_progress > 0) {
    msg = chSemWaitTimeout(&flash_operation_semaphore, MS2ST(100)); // Very short timeout during calibration
  } else {
    msg = chSemWaitTimeout(&flash_operation_semaphore, MS2ST(500)); // 500ms timeout normally
  }
  
  // If we can't get the semaphore within timeout, return error
  if (msg != MSG_OK) {
    return -1;  // Failed to get access to flash
  }
  
  // Apply magic word and calculate checksum
  config.magic = CONFIG_MAGIC;
  config.checksum = checksum(&config, sizeof config - sizeof config.checksum);

  // write to flash
  flash_program_half_word_buffer((uint16_t*)SAVE_CONFIG_ADDR, (uint16_t*)&config, sizeof(config_t));
  int result = memcmp((const void *)SAVE_CONFIG_ADDR, &config, sizeof config) ? -1 : 0;
  
  // Release the semaphore
  chSemSignal(&flash_operation_semaphore);
  
  if (result == 0 && config_event_bus != NULL) {
    event_bus_publish(config_event_bus, EVENT_STORAGE_UPDATED, NULL);
  }
  return result;
}

static int config_recall_impl(void) {
  const config_t* src = (const config_t*)SAVE_CONFIG_ADDR;

  if (src->magic != CONFIG_MAGIC ||
      checksum(src, sizeof *src - sizeof src->checksum) != src->checksum ||
      src->_bandwidth > 511 || src->_lever_mode > LM_EDELAY || src->_brightness > 100)
    return -1;
  // duplicated saved data onto sram to be able to modify marker/trace
  memcpy(&config, src, sizeof(config_t));
  return 0;
}

static int save_properties(uint32_t id, bool antenna) {
  if (id >= SAVEAREA_MAX)
    return -1;
#ifdef NANOVNA_F303
  if (id >= ANT_CAL_FIRST_SLOT && !antenna)
    return -1;
  if (current_props.magic == ANT_CAL_MAGIC && !antenna_cal_valid(&current_props))
    return -1; // Never relabel a nonuniform grid as legacy uniform calibration.
#endif

  // Don't save during critical calibration phase to prevent data corruption
  if (calibration_in_progress && !antenna) {
    return -1;  // Return error to indicate save was denied
  }

  // Wait for exclusive access to flash operations
  msg_t msg = chSemWaitTimeout(&flash_operation_semaphore, MS2ST(500));
  if (msg != MSG_OK)
    return -1;

  // Apply magic word and calculate checksum
#ifdef NANOVNA_F303
  if (!antenna_cal_valid(&current_props))
#endif
    current_props.magic = PROPERTIES_MAGIC;
  current_props.checksum =
      checksum(&current_props, sizeof current_props - sizeof current_props.checksum);
  if (!properties_valid(&current_props)) {
    chSemSignal(&flash_operation_semaphore);
    return -1;
  }

  // write to flash
  uint16_t* dst = (uint16_t*)(uintptr_t)calibration_slot_area(id);
  flash_program_half_word_buffer(dst, (uint16_t*)&current_props, sizeof(properties_t));
  checksum_ok &= ~(1u << id);
  int result = memcmp(dst, &current_props, sizeof current_props) ? -1 : 0;
  if (result == 0) lastsaveid = id;
  
  chSemSignal(&flash_operation_semaphore);
  return result;
}

static int caldata_save_impl(uint32_t id) { return save_properties(id, false); }

#ifdef NANOVNA_F303
_Static_assert(ANT_CAL_SCRATCH >= SAVE_CONFIG_ADDR + SAVE_CONFIG_SIZE, "ANT staging overlaps saved storage");
_Static_assert(ANT_CAL_SCRATCH + 6 * ANT_CAL_PLANE_BYTES <= 0x08040000u, "ANT staging exceeds H4 flash");
/* Dedicated writer: ordinary UI, shell and autosave cannot write ANT slots. */
int antenna_cal_store(unsigned profile) {
  if (profile > 1 || antenna_cal_stage() != 4 ||
      !antenna_cal_valid(&current_props) || current_props._reserved1[0] != profile)
    return -1;
  return save_properties(ANT_CAL_FIRST_SLOT + profile, true);
}
int antenna_cal_stage_write(unsigned plane, const void *data, unsigned size) {
  if (plane >= 6 || size != ANT_CAL_POINTS * 2 * sizeof(float) ||
      antenna_cal_stage() < 1 || antenna_cal_stage() > 3)
    return -1;
  if (chSemWaitTimeout(&flash_operation_semaphore, MS2ST(500)) != MSG_OK) return -1;
  void *dst = (void *)(uintptr_t)(ANT_CAL_SCRATCH + plane * ANT_CAL_PLANE_BYTES);
  flash_program_half_word_buffer(dst, (uint16_t *)data, size);
  int result = memcmp(dst, data, size) ? -1 : 0;
  chSemSignal(&flash_operation_semaphore);
  return result;
}
#endif

const properties_t* get_properties(uint32_t id) {
  if (id >= SAVEAREA_MAX)
    return NULL;
  // point to saved area on the flash memory
  properties_t* src = (properties_t*)(uintptr_t)calibration_slot_area(id);
  // Check crc cache mask (made it only 1 time)
  if (checksum_ok & (1 << id))
    return src;
  if (!properties_valid(src))
    return NULL;
  checksum_ok |= 1 << id;
  return src;
}

static int caldata_recall_impl(uint32_t id) {
  lastsaveid = NO_SAVE_SLOT;
  if (id == NO_SAVE_SLOT)
    return 0;

  // Wait for exclusive access to flash operations (read consistency)
  msg_t msg = chSemWaitTimeout(&flash_operation_semaphore, MS2ST(500));
  if (msg != MSG_OK)
    return -1;

  // point to saved area on the flash memory
  const properties_t* src = get_properties(id);
  if (src == NULL) {
    chSemSignal(&flash_operation_semaphore);
    //  load_default_properties();
    return 1;
  }
  // active configuration points to save data on flash memory
  lastsaveid = id;
  // duplicated saved data onto sram to be able to modify marker/trace
  memcpy(&current_props, src, sizeof(properties_t));
  
  chSemSignal(&flash_operation_semaphore);
  return 0;
}

static void clear_all_config_prop_data_impl(void) {
  // Wait for exclusive access to flash operations with timeout
  msg_t msg = chSemWaitTimeout(&flash_operation_semaphore, MS2ST(2000)); // 2 second timeout for erase operation
  
  // If we can't get the semaphore within timeout, return early
  if (msg != MSG_OK) {
    return;  // Failed to get access to flash
  }
  
  lastsaveid = NO_SAVE_SLOT;
  checksum_ok = 0;
  // unlock and erase flash pages
  // Clear Config resets all saved calibration slots, including both ANT profiles.
  flash_erase_pages(SAVE_PROP_CONFIG_ADDR, SAVE_FULL_AREA_SIZE);
  
  // Release the semaphore
  chSemSignal(&flash_operation_semaphore);
  
  if (config_event_bus != NULL) {
    event_bus_publish(config_event_bus, EVENT_STORAGE_UPDATED, NULL);
  }
}

static const config_service_api_t api = {
    .save_configuration = config_save_impl,
    .load_configuration = config_recall_impl,
    .save_calibration = caldata_save_impl,
    .load_calibration = caldata_recall_impl,
    .erase_calibration = clear_all_config_prop_data_impl,
};

static bool initialized = false;

static void config_service_init_semaphore(void) {
  chSemObjectInit(&flash_operation_semaphore, 1);  // Initialize with 1 (available)
}

void config_service_init(void) {
  config_service_init_semaphore();
  initialized = true;
}

void config_service_attach_event_bus(event_bus_t* bus) {
  if (config_event_bus == bus) {
    return;
  }
  config_event_bus = bus;
  if (bus != NULL) {
    event_bus_subscribe(bus, EVENT_CONFIGURATION_CHANGED, config_on_configuration_changed, NULL);
  }
}

void config_service_notify_configuration_changed(void) {
  if (config_event_bus != NULL) {
    event_bus_publish(config_event_bus, EVENT_CONFIGURATION_CHANGED, NULL);
  }
}

const config_service_api_t* config_service_api(void) {
  return initialized ? &api : NULL;
}

static const config_service_api_t* require_api(void) {
  const config_service_api_t* instance = config_service_api();
  return instance;
}

int config_save(void) {
  const config_service_api_t* instance = require_api();
  return instance ? instance->save_configuration() : -1;
}

int config_recall(void) {
  const config_service_api_t* instance = require_api();
  return instance ? instance->load_configuration() : -1;
}

int caldata_save(uint32_t id) {
  const config_service_api_t* instance = require_api();
  return instance ? instance->save_calibration(id) : -1;
}

int caldata_recall(uint32_t id) {
  const config_service_api_t* instance = require_api();
  return instance ? instance->load_calibration(id) : -1;
}

void clear_all_config_prop_data(void) {
  const config_service_api_t* instance = require_api();
  if (instance && instance->erase_calibration) {
    instance->erase_calibration();
  }
}
