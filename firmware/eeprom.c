/*
 * Bafang LCD 850C firmware
 *
 * Copyright (C) Casainho, 2018.
 *
 * Released under the GPL License, Version 3
 */

#include "stdio.h"
#include <string.h>
#include "eeprom.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "app_error.h"
#include "fds.h"
#include "nrf_delay.h"
#define WAIT_TIME 1000 //wait 1 seconds before a reset
#define CONFIG_FILE (0x8013)
#define CONFIG_REC_KEY (0x7015)
fds_record_desc_t m_desc_config = {0};

static configurations_t m_configurations;

/* Flag to check fds initialization. */
static bool volatile m_fds_initialized;

fds_record_t record =
    {
        .file_id = CONFIG_FILE,
        .key = CONFIG_REC_KEY,
        .data.p_data = &m_configurations,
        //The length of a data record is always expressed in 4-byte units (words).
        //.data.length_words = (sizeof(m_configurations)) / sizeof(uint32_t),
        .data.length_words = 5,
};

static void fds_evt_handler(fds_evt_t const *p_evt)
{
  switch (p_evt->id)
  {
  case FDS_EVT_INIT:
    if (p_evt->result == NRF_SUCCESS)
    {
      m_fds_initialized = true;
    }
    break;

  case FDS_EVT_WRITE:
  {
    if (p_evt->result == NRF_SUCCESS)
    {
    }
  }
  break;

  case FDS_EVT_DEL_RECORD:
  {
  }
  break;

  default:
    break;
  }
}
/**@brief   Wait for fds to initialize. */
static void wait_for_fds_ready(void)
{
  while (!m_fds_initialized)
  {
    // power_manage();
  }
}
void wait_and_reset(void)
{
  nrf_delay_ms(WAIT_TIME);
  sd_nvic_SystemReset(); //reset and start again
}

void eeprom_init(uint8_t *ant_id, uint8_t *bluetooth_flag, uint8_t *ebike_flag, uint8_t *garmin_flag, uint8_t *brake_flag)
{
  ret_code_t err_code;

  (void)fds_register(fds_evt_handler);
  err_code = fds_init();
  APP_ERROR_CHECK(err_code);
  wait_for_fds_ready();

  fds_flash_record_t config = {0};
  fds_find_token_t m_tok_config = {0};

  //finfd any previous flash records
  err_code = fds_record_find(CONFIG_FILE, CONFIG_REC_KEY, &m_desc_config, &m_tok_config); // see if a record is found for config file
  //err_code = fds_record_find_by_key(CONFIG_REC_KEY, &m_desc_config, &m_tok_config);
  if (err_code == NRF_SUCCESS)
  {
    /* Open the record and read its contents. */
    err_code = fds_record_open(&m_desc_config, &config);
    APP_ERROR_CHECK(err_code);
    /* Copy the configuration from flash into m_dummy_cfg. */
    memcpy(&m_configurations, config.p_data, sizeof(m_configurations));
    /* Close the record when done reading. */
    err_code = fds_record_close(&m_desc_config);
    APP_ERROR_CHECK(err_code);
    //change the ant id and bluetooth flag
    *ant_id = m_configurations.ui8_ant_device_id;
    *bluetooth_flag = m_configurations.ui8_bluetooth_flag;
    *ebike_flag = m_configurations.ui8_ant_lev_flag;
    *garmin_flag = m_configurations.ui8_ant_controls_flag;
    *brake_flag = m_configurations.ui8_brake_flag;
  }
  else // no flash record, write DEFAULTS TO FLASH and reset
  {
    m_configurations.ui8_ant_device_id = 0;
    m_configurations.ui8_bluetooth_flag = 0;
    m_configurations.ui8_ant_lev_flag = 1;
    m_configurations.ui8_ant_controls_flag = 0;
    err_code = fds_record_write(&m_desc_config, &record);
    APP_ERROR_CHECK(err_code);
    wait_and_reset();
  }
}
void eeprom_write_variables(uint8_t ant_num, uint8_t bluetooth, uint8_t ebike, uint8_t garmin, uint8_t brake)
{
  //delete previous records
  ret_code_t err_code;
  m_configurations.ui8_ant_device_id = ant_num;
  m_configurations.ui8_bluetooth_flag = bluetooth;
  m_configurations.ui8_ant_lev_flag = ebike;
  m_configurations.ui8_ant_controls_flag = garmin;
  m_configurations.ui8_brake_flag = brake;
  //now update the  record

  err_code = fds_record_update(&m_desc_config, &record);
  APP_ERROR_CHECK(err_code);

  wait_and_reset();
}
