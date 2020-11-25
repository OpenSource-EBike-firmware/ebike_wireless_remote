/*
 * TSDZ2 EBike wireless remote firmware
 *
 * Copyright (C) Casainho, 2020
 * Copyright (C) Rananna, 2020
 *
 * Released under the GPL License, Version 3
 */

#include <stdio.h>
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf.h"
#include "hardfault.h"
#include "app_error.h"
#include "app_timer.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "ble.h"
#include "ble_err.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "ble_conn_state.h"
#include "peer_manager.h"
#include "peer_manager_handler.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "ble_services.h"
#include "nrf_sdh_ant.h"
#include "ant_key_manager.h"
#include "ant_lev.h"
#include "pins.h"
#include "app_util_platform.h"
#include "ant_interface.h"
#include "nrf_delay.h"
#include "fds.h"

#include "app_button.h"
#include "nrf_drv_clock.h"
#include "sdk_config.h"
#include "ant_state_indicator.h"
#include "bsp_btn_ant.h"
#include "antplus_controls.h"
#include "eeprom.h"
#include "nrf_sdh_soc.h"
#include "nrf_power.h"
#include "nrf_bootloader_info.h"
#include "custom_board.h"

#define BUTTON_DETECTION_DELAY APP_TIMER_TICKS(50)           /**< Delay from a GPIOTE event until a button is reported as pushed (in number of timer ticks). */
#define BUTTON_PRESS_TIMEOUT APP_TIMER_TICKS(60 * 60 * 1000) // 1h to enter low power mode
#define BUTTON_LONG_PRESS_TIMEOUT APP_TIMER_TICKS(1000)      // 1 seconds for long press
#define BUTTON_DFU_PRESS_TIMEOUT APP_TIMER_TICKS(10000)      //10 seconds
#define DEVICE_NAME "TSDZ2_wireless_remote"                  /**< Name of device. Will be included in the advertising data. */

#define APP_BLE_CONN_CFG_TAG 1 /**< A tag identifying the SoftDevice BLE configuration. */

#define APP_ADV_INTERVAL 64                                    /**< The advertising interval (in units of 0.625 ms; this value corresponds to 40 ms). */
#define APP_ADV_DURATION BLE_GAP_ADV_TIMEOUT_GENERAL_UNLIMITED /**< The advertising time-out (in units of seconds). When set to 0, we will never time out. */

#define MIN_CONN_INTERVAL MSEC_TO_UNITS(100, UNIT_1_25_MS) /**< Minimum acceptable connection interval (0.5 seconds). */
#define MAX_CONN_INTERVAL MSEC_TO_UNITS(200, UNIT_1_25_MS) /**< Maximum acceptable connection interval (1 second). */
#define SLAVE_LATENCY 0                                    /**< Slave latency. */
#define CONN_SUP_TIMEOUT MSEC_TO_UNITS(4000, UNIT_10_MS)   /**< Connection supervisory time-out (4 seconds). */

#define FIRST_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(20000) /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (15 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(5000)   /**< Time between each call to sd_ble_gap_conn_param_update after the first call (5 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT 3                        /**< Number of attempts before giving up the connection parameter negotiation. */

#define SEC_PARAM_BOND 1                               /**< Perform bonding. */
#define SEC_PARAM_MITM 0                               /**< Man In The Middle protection not required. */
#define SEC_PARAM_LESC 0                               /**< LE Secure Connections not enabled. */
#define SEC_PARAM_KEYPRESS 0                           /**< Keypress notifications not enabled. */
#define SEC_PARAM_IO_CAPABILITIES BLE_GAP_IO_CAPS_NONE /**< No I/O capabilities. */
#define SEC_PARAM_OOB 0                                /**< Out Of Band data not available. */
#define SEC_PARAM_MIN_KEY_SIZE 7                       /**< Minimum encryption key size. */
#define SEC_PARAM_MAX_KEY_SIZE 16                      /**< Maximum encryption key size. */

#define APP_FEATURE_NOT_SUPPORTED BLE_GATT_STATUS_ATTERR_APP_BEGIN + 2 /**< Reply when unsupported features are requested. */

NRF_BLE_GATT_DEF(m_gatt);           /**< GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr);             /**< Context for the Queued Write module.*/
BLE_ADVERTISING_DEF(m_advertising); /**< Advertising module instance. */
BLE_ANT_ID_DEF(m_ble_ant_id_service);
//test flash write completed
static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;                /**< Handle of the current connection. */
static uint8_t m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;           /**< Advertising handle used to identify an advertising set. */
static uint8_t m_enc_advdata[BLE_GAP_ADV_SET_DATA_SIZE_MAX];            /**< Buffer for storing an encoded advertising set. */
static uint8_t m_enc_scan_response_data[BLE_GAP_ADV_SET_DATA_SIZE_MAX]; /**< Buffer for storing an encoded scan data. */
uint8_t turn_bluetooth_on = 0;                                          //needs to be a flag to manage flash write events
uint8_t turn_bluetooth_off = 0;                                         //needs to be a flag to manage flash write events
/**@brief Struct that contains pointers to the encoded advertising data. */

static ble_gap_adv_data_t m_adv_data =
    {
        .adv_data =
            {
                .p_data = m_enc_advdata,
                .len = BLE_GAP_ADV_SET_DATA_SIZE_MAX},
        .scan_rsp_data =
            {
                .p_data = m_enc_scan_response_data,
                .len = BLE_GAP_ADV_SET_DATA_SIZE_MAX}};

static ble_uuid_t m_adv_uuids[] = /**< Universally unique service identifiers. */
    {
        {BLE_UUID_HEALTH_THERMOMETER_SERVICE, BLE_UUID_TYPE_BLE},
        {BLE_UUID_BATTERY_SERVICE, BLE_UUID_TYPE_BLE},
        {BLE_UUID_DEVICE_INFORMATION_SERVICE, BLE_UUID_TYPE_BLE}};

/**@brief Clear bond information from persistent storage.
 */
uint32_t ui32_seconds_since_startup = 0;
//uint32_t err_code=0;
volatile uint32_t main_ticks;
uint8_t enable_bluetooth = 0;

static void delete_bonds(void)
{
  ret_code_t err_code;

  NRF_LOG_INFO("Erase bonds!");

  err_code = pm_peers_delete();
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for starting advertising.
 */
static void advertising_start(bool erase_bonds)
{
  if (erase_bonds == true)
  {
    delete_bonds();
    // Advertising is started by PM_EVT_PEERS_DELETE_SUCCEEDED event.
  }
  else
  {
    uint32_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
    APP_ERROR_CHECK(err_code);
  }
}

/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context)
{
  ret_code_t err_code;

  switch (p_ble_evt->header.evt_id)
  {
  case BLE_GAP_EVT_CONNECTED:
    NRF_LOG_INFO("Connected");
    m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
    err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
    APP_ERROR_CHECK(err_code);
    break;

  case BLE_GAP_EVT_DISCONNECTED:
    NRF_LOG_INFO("Disconnected");
    m_conn_handle = BLE_CONN_HANDLE_INVALID;
    advertising_start(false);
    break;

  case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
    break;

  case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
  {
    NRF_LOG_DEBUG("PHY update request.");
    ble_gap_phys_t const phys =
        {
            .rx_phys = BLE_GAP_PHY_AUTO,
            .tx_phys = BLE_GAP_PHY_AUTO,
        };
    err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
    APP_ERROR_CHECK(err_code);
  }
  break;

  case BLE_GATTS_EVT_SYS_ATTR_MISSING:
    // No system attributes have been stored.
    err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
    APP_ERROR_CHECK(err_code);
    break;

  case BLE_GATTC_EVT_TIMEOUT:
    // Disconnect on GATT Client timeout event.
    NRF_LOG_DEBUG("GATT Client Timeout.");
    err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                     BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    APP_ERROR_CHECK(err_code);
    break;

  case BLE_GATTS_EVT_TIMEOUT:
    // Disconnect on GATT Server timeout event.
    NRF_LOG_DEBUG("GATT Server Timeout.");
    err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                     BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    APP_ERROR_CHECK(err_code);
    break;

  case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
  {
    ble_gatts_evt_rw_authorize_request_t req;
    ble_gatts_rw_authorize_reply_params_t auth_reply;

    req = p_ble_evt->evt.gatts_evt.params.authorize_request;

    if (req.type != BLE_GATTS_AUTHORIZE_TYPE_INVALID)
    {
      if ((req.request.write.op == BLE_GATTS_OP_PREP_WRITE_REQ) ||
          (req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW) ||
          (req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL))
      {
        if (req.type == BLE_GATTS_AUTHORIZE_TYPE_WRITE)
        {
          auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
        }
        else
        {
          auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
        }
        auth_reply.params.write.gatt_status = APP_FEATURE_NOT_SUPPORTED;
        sd_ble_gatts_rw_authorize_reply(p_ble_evt->evt.gatts_evt.conn_handle, &auth_reply);
      }
    }
  }
  break; // BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST

  default:
    // No implementation needed.
    break;
  }
}

//APP_TIMER_DEF(m_lev_send);
APP_TIMER_DEF(m_timer_button_press_timeout);
APP_TIMER_DEF(m_timer_button_long_press_timeout);
APP_TIMER_DEF(m_timer_button_dfu_press_timeout);

//APP_TIMER_DEF(m_antplus_controls_send);

button_pins_t m_buttons_wait_to_send = 0;
bool m_timer_buttons_send_running = false;
bool m_button_long_press = false;

//set default  old ant ID for reset;

uint8_t old_ant_device_id = 0; //initially in pairing mode
uint8_t new_ant_device_id = 0; // used to check for change of ant id
#define MSEC_PER_TICK 10
APP_TIMER_DEF(main_timer);
#define MAIN_INTERVAL APP_TIMER_TICKS(MSEC_PER_TICK)

void shutdown(void);
#define CONTROLS_HW_REVISION 2
#define CONTROLS_MANUFACTURER_ID 255
#define CONTROLS_MODEL_NUMBER 2
#define CONTROLS_SW_REVISION_MAJOR 2
#define CONTROLS_SW_REVISION_MINOR 2
#define CONTROLS_SERIAL_NUMBER 3241
#define CONTROLS_CHANNEL_NUM 1 // ?? seems can be any value from 0 to 8
#define ANT_LEV_ANT_OBSERVER_PRIO 1
#define LEV_HW_REVISION 1
#define LEV_MANUFACTURER_ID 254
#define LEV_MODEL_NUMBER 1
#define LEV_SW_REVISION_MAJOR 1
#define LEV_SW_REVISION_MINOR 1
#define LEV_SERIAL_NUMBER 1234
#define LEV_CHANNEL_NUM 0             // ?? seems can be any value from 0 to 8
#define CONTROLS_CHAN_ID_TRANS_TYPE 0 // LEV table 4.2
#define LEV_CHAN_ID_TRANS_TYPE 0
#define CONTROLS_CHAN_ID_DEV_NUM 0 // [1 - 65535], LEV table 4.2 0 for wildcard
#define LEV_CHAN_ID_DEV_NUM 0
#define LEV_ANTPLUS_NETWORK_NUM 0
#define CONTROLS_ANTPLUS_NETWORK_NUM 1
#define ANTPLUS_CONTROLS_ANT_OBSERVER_PRIO 1
// @snippet [ANT LEV RX Instance]
void ant_lev_evt_handler(ant_lev_profile_t *p_profile, ant_lev_evt_t event);
void antplus_controls_evt_handler(antplus_controls_profile_t *p_profile, antplus_controls_evt_t event);

LEV_DISP_CHANNEL_CONFIG_DEF(m_ant_lev,
                            LEV_CHANNEL_NUM,
                            LEV_CHAN_ID_TRANS_TYPE,
                            LEV_CHAN_ID_DEV_NUM,
                            LEV_ANTPLUS_NETWORK_NUM,
                            LEV_MSG_PERIOD_4Hz);

CONTROLS_SENS_CHANNEL_CONFIG_DEF(m_antplus_controls,
                                 CONTROLS_CHANNEL_NUM,
                                 CONTROLS_CHAN_ID_TRANS_TYPE,
                                 CONTROLS_CHAN_ID_DEV_NUM,
                                 CONTROLS_ANTPLUS_NETWORK_NUM);

CONTROLS_SENS_PROFILE_CONFIG_DEF(m_antplus_controls,
                                 antplus_controls_evt_handler);

static ant_lev_profile_t m_ant_lev;
static antplus_controls_profile_t m_antplus_controls;

NRF_SDH_ANT_OBSERVER(m_ant_observer, ANT_LEV_ANT_OBSERVER_PRIO, ant_lev_disp_evt_handler, &m_ant_lev);
NRF_SDH_ANT_OBSERVER(m_ant_observer_control, ANTPLUS_CONTROLS_ANT_OBSERVER_PRIO, antplus_controls_sens_evt_handler, &m_antplus_controls);

uint16_t cnt_1;

void antplus_controls_evt_handler(antplus_controls_profile_t *p_profile, antplus_controls_evt_t event)
{
  nrf_pwr_mgmt_feed();

  switch (event)
  {
  case ANTPLUS_CONTROLS_PAGE_73_UPDATED:
    break;

  default:
    break;
  }
}

void ant_lev_evt_handler(ant_lev_profile_t *p_profile, ant_lev_evt_t event)
{
  nrf_pwr_mgmt_feed();

  switch (event)
  {
  case ANT_LEV_PAGE_1_UPDATED:
      //  p_profile->page_16.travel_mode = p_profile->common.travel_mode_state;
      ;
    break;

  case ANT_LEV_PAGE_2_UPDATED:
    break;

  case ANT_LEV_PAGE_3_UPDATED:

    break;

  case ANT_LEV_PAGE_4_UPDATED:
    break;

  case ANT_LEV_PAGE_5_UPDATED:
    break;

  case ANT_LEV_PAGE_34_UPDATED:

    break;

  case ANT_LEV_PAGE_16_UPDATED:

    break;

  case ANT_LEV_PAGE_80_UPDATED:

    break;

  case ANT_LEV_PAGE_81_UPDATED:

    break;

  case ANT_LEV_PAGE_REQUEST_SUCCESS:
    break;

  case ANT_LEV_PAGE_REQUEST_FAILED:
    break;

  default:
    break;
  }
}
static void main_timer_timeout(void *p_context)
{

  UNUSED_PARAMETER(p_context);

  main_ticks++; //updated every 10ms, 100 for every second

  if (main_ticks % (1000 / MSEC_PER_TICK) == 0)
    ui32_seconds_since_startup++;
}
static void timer_button_press_timeout_handler(void *p_context)
{
  UNUSED_PARAMETER(p_context);

  // enter ultra low power mode
  shutdown();
}
static void timer_button_long_press_timeout_handler(void *p_context)
{
  UNUSED_PARAMETER(p_context);

  m_button_long_press = true;
}
static void timer_button_dfu_press_timeout_handler(void *p_context)
{
  UNUSED_PARAMETER(p_context);
  nrf_power_gpregret_set(BOOTLOADER_DFU_START);
  sd_nvic_SystemReset(); //reset and start again
}

static void button_event_handler(uint8_t pin_no, uint8_t button_action)
{

  button_pins_t button_pin = (button_pins_t)pin_no;
  ret_code_t err_code;

  switch (button_action)
  {

  case APP_BUTTON_RELEASE: //process the button actions
  {                        // button released
    if (!m_button_long_press) //not a long press
    {

      if (button_pin == MINUS__PIN)
      //motor assist increase
      {
        buttons_send_page16(&m_ant_lev, button_pin, m_button_long_press);
      }
      else if (button_pin == PLUS__PIN)
      //motor assist decrease
      {
        buttons_send_page16(&m_ant_lev, button_pin, m_button_long_press);
      }
      else if (button_pin == ENTER__PIN)
      //pageup on bike computer
      {
        buttons_send_pag73(&m_antplus_controls, button_pin);
      }
      else if (button_pin == STANDBY__PIN)
      //unassigned
      {
        //turn off the motor power
        // buttons_send_pag73(&m_antplus_controls, button_pin);
      }
    }
    else
    //long press actions
    {
      //long press actions
      if (button_pin == MINUS__PIN)
      // store bluetooth flag and reset
      {
        turn_bluetooth_off = 1; // disable BLUETOOTH on restart
      }
      else if (button_pin == PLUS__PIN)
      {

        // set flag to enable bluetooth on restart
        turn_bluetooth_on = 1;
      }
      else if (button_pin == ENTER__PIN)
      {
        //unassigned
      }
      else if (button_pin == STANDBY__PIN)
      {
        //unassigned
      }

      m_button_long_press = false;
    }
    //reset the button timers
    err_code = app_timer_stop(m_timer_button_press_timeout); //1hr timeout for low power
    APP_ERROR_CHECK(err_code);
    err_code = app_timer_start(m_timer_button_press_timeout, BUTTON_PRESS_TIMEOUT, NULL);
    APP_ERROR_CHECK(err_code);
    err_code = app_timer_stop(m_timer_button_long_press_timeout); //stop the long press timerf
    APP_ERROR_CHECK(err_code);
    err_code = app_timer_stop(m_timer_button_dfu_press_timeout); //stop the long press timerf
    APP_ERROR_CHECK(err_code);
    break;
  }
  case APP_BUTTON_PUSH: //button pushed
  {

    //start long button timer

    err_code = app_timer_stop(m_timer_button_long_press_timeout); //stop the long press timerf
    APP_ERROR_CHECK(err_code);
    err_code = app_timer_start(m_timer_button_long_press_timeout, BUTTON_LONG_PRESS_TIMEOUT, NULL); //start the long press timerf
    APP_ERROR_CHECK(err_code);
    m_button_long_press = false;
    err_code = app_timer_stop(m_timer_button_dfu_press_timeout); //stop the long press timerf
    APP_ERROR_CHECK(err_code);
    err_code = app_timer_start(m_timer_button_dfu_press_timeout, BUTTON_DFU_PRESS_TIMEOUT, NULL); //start the long press timerf
    APP_ERROR_CHECK(err_code);


    break;
  }
  }
}

void buttons_init(void)
{
  ret_code_t err_code;

  //The array must be static because a pointer to it will be saved in the button handler module.
  static app_button_cfg_t buttons[5] =
      {
          {(uint8_t)PLUS__PIN, APP_BUTTON_ACTIVE_LOW, 1, GPIO_PIN_CNF_PULL_Pullup, button_event_handler},
          {(uint8_t)MINUS__PIN, APP_BUTTON_ACTIVE_LOW, 1, GPIO_PIN_CNF_PULL_Pullup, button_event_handler},
          {(uint8_t)ENTER__PIN, APP_BUTTON_ACTIVE_LOW, 1, GPIO_PIN_CNF_PULL_Pullup, button_event_handler},
          {(uint8_t)STANDBY__PIN, APP_BUTTON_ACTIVE_LOW, 1, GPIO_PIN_CNF_PULL_Pullup, button_event_handler},
          {(uint8_t)BUTTON_1, APP_BUTTON_ACTIVE_LOW, 1, GPIO_PIN_CNF_PULL_Pullup, button_event_handler}};

  err_code = app_button_init(buttons, ARRAY_SIZE(buttons), BUTTON_DETECTION_DELAY);
  // this will enable wakeup from ultra low power mode (any button press)
  nrf_gpio_cfg_sense_input(PLUS__PIN, GPIO_PIN_CNF_PULL_Pullup, GPIO_PIN_CNF_SENSE_Low);
  nrf_gpio_cfg_sense_input(MINUS__PIN, GPIO_PIN_CNF_PULL_Pullup, GPIO_PIN_CNF_SENSE_Low);
  nrf_gpio_cfg_sense_input(ENTER__PIN, GPIO_PIN_CNF_PULL_Pullup, GPIO_PIN_CNF_SENSE_Low);
  nrf_gpio_cfg_sense_input(STANDBY__PIN, GPIO_PIN_CNF_PULL_Pullup, GPIO_PIN_CNF_SENSE_Low);
  nrf_gpio_cfg_sense_input(BOOTLOADER__PIN, GPIO_PIN_CNF_PULL_Pullup, GPIO_PIN_CNF_SENSE_Low);

  if (err_code == NRF_SUCCESS)
  {
    err_code = app_button_enable();
  }
  APP_ERROR_CHECK(err_code);

  err_code = app_timer_create(&m_timer_button_press_timeout,
                              APP_TIMER_MODE_SINGLE_SHOT,
                              timer_button_press_timeout_handler);
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_create(&m_timer_button_long_press_timeout,
                              APP_TIMER_MODE_SINGLE_SHOT,
                              timer_button_long_press_timeout_handler);

  APP_ERROR_CHECK(err_code);
  err_code = app_timer_create(&m_timer_button_dfu_press_timeout,
                              APP_TIMER_MODE_SINGLE_SHOT,
                              timer_button_dfu_press_timeout_handler);

  APP_ERROR_CHECK(err_code);

  err_code = app_timer_start(m_timer_button_press_timeout, BUTTON_PRESS_TIMEOUT, NULL);
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_stop(m_timer_button_long_press_timeout); //stop the long press timer
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_stop(m_timer_button_dfu_press_timeout); //stop the long press timer
  APP_ERROR_CHECK(err_code);
}
void shutdown(void)
{
  // // all pins must be disabled or system will wakeup, similar to a reset after enter ultra low power mode
  // // debug pins
  // nrf_gpio_cfg_default(10);
  // nrf_gpio_cfg_default(9);

  // enter in ultra low power mode
  nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_SYSOFF);
}

static void profile_setup(void)
{
  uint32_t err_code;

  // fill battery status data page.
  m_antplus_controls.page_82 = ANTPLUS_CONTROLS_PAGE82(295); // battery 2.95 volts, fully charged

  // fill manufacturer's common data page.

  m_antplus_controls.page_80 = ANT_COMMON_page80(CONTROLS_HW_REVISION,
                                                 CONTROLS_MANUFACTURER_ID,
                                                 CONTROLS_MODEL_NUMBER);
  // fill product's common data page.
  m_antplus_controls.page_81 = ANT_COMMON_page81(CONTROLS_SW_REVISION_MAJOR,
                                                 CONTROLS_SW_REVISION_MINOR,
                                                 CONTROLS_SERIAL_NUMBER);

  m_ant_lev.page_80 = ANT_COMMON_page80(LEV_HW_REVISION,
                                        LEV_MANUFACTURER_ID,
                                        LEV_MODEL_NUMBER);
  // fill product's common data page.
  m_ant_lev.page_81 = ANT_COMMON_page81(LEV_SW_REVISION_MAJOR,
                                        LEV_SW_REVISION_MINOR,
                                        LEV_SERIAL_NUMBER);

  //@snippet [ANT LEV RX Profile Setup]

  //retrieve the old ant id from a power cycle

  m_ant_lev_channel_lev_disp_config.device_number = old_ant_device_id;

  err_code = ant_lev_disp_init(&m_ant_lev, LEV_DISP_CHANNEL_CONFIG(m_ant_lev), ant_lev_evt_handler);
  APP_ERROR_CHECK(err_code);
  err_code = antplus_controls_sens_init(&m_antplus_controls,
                                        CONTROLS_SENS_CHANNEL_CONFIG(m_antplus_controls),
                                        CONTROLS_SENS_PROFILE_CONFIG(m_antplus_controls));
  APP_ERROR_CHECK(err_code);
  err_code = antplus_controls_sens_open(&m_antplus_controls);
  APP_ERROR_CHECK(err_code);
  err_code = ant_lev_disp_open(&m_ant_lev);
  APP_ERROR_CHECK(err_code);

  err_code = ant_state_indicator_channel_opened();
  APP_ERROR_CHECK(err_code);
}

static void softdevice_setup(void)
{
  ret_code_t err_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(err_code);

  ASSERT(nrf_sdh_is_enabled());

  err_code = nrf_sdh_ant_enable();
  APP_ERROR_CHECK(err_code);
  err_code = ant_plus_key_set(CONTROLS_ANTPLUS_NETWORK_NUM);
  APP_ERROR_CHECK(err_code);
  err_code = ant_plus_key_set(LEV_ANTPLUS_NETWORK_NUM);
  APP_ERROR_CHECK(err_code);
}

static void lfclk_config(void)
{
  ret_code_t err_code = nrf_drv_clock_init();
  APP_ERROR_CHECK(err_code);

  nrf_drv_clock_lfclk_request(NULL);
}
/*
static void timer_init(void)
{
  ret_code_t err_code = app_timer_init();
  APP_ERROR_CHECK(err_code);
}
*/
/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
  ret_code_t err_code;
  /*
  err_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(err_code);
*/
  // Configure the BLE stack using the default settings.
  // Fetch the start address of the application RAM.
  uint32_t ram_start = 0;
  err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
  APP_ERROR_CHECK(err_code);
  ram_start += 32;
  //ram_start += 10028;
  // Enable BLE stack.
  err_code = nrf_sdh_ble_enable(&ram_start);
  APP_ERROR_CHECK(err_code);

  // Register a handler for BLE events.
  NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
static void gap_params_init(void)
{
  ret_code_t err_code;
  ble_gap_conn_params_t gap_conn_params;
  ble_gap_conn_sec_mode_t sec_mode;

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

  err_code = sd_ble_gap_device_name_set(&sec_mode,
                                        (const uint8_t *)DEVICE_NAME,
                                        strlen(DEVICE_NAME));
  APP_ERROR_CHECK(err_code);

  memset(&gap_conn_params, 0, sizeof(gap_conn_params));

  gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
  gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
  gap_conn_params.slave_latency = SLAVE_LATENCY;
  gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;

  err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the GATT module.
 */
static void gatt_init(void)
{
  ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling Queued Write Module errors.
 *
 * @details A pointer to this function will be passed to each service which may need to inform the
 *          application about an error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error)
{
  APP_ERROR_HANDLER(nrf_error);
}

static void ant_id_write_handler(uint16_t conn_handle, ble_ant_id_t *p_ant_id, uint8_t value)
{
  new_ant_device_id = value;
}

/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void)
{
  ret_code_t err_code;
  ble_ant_id_init_t init = {0};
  nrf_ble_qwr_init_t qwr_init = {0};

   // Initialize Queued Write Module.
  qwr_init.error_handler = nrf_qwr_error_handler;

  err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
  APP_ERROR_CHECK(err_code);

  init.ant_id_write_handler = ant_id_write_handler;

  err_code = ble_service_ant_id_init(&m_ble_ant_id_service, &init);
  APP_ERROR_CHECK(err_code);

  ble_ant_id_on_change(m_conn_handle, &m_ble_ant_id_service, old_ant_device_id);
}

/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
  switch (ble_adv_evt)
  {
  case BLE_ADV_EVT_FAST:
    NRF_LOG_INFO("Fast advertising.");
    break;

  case BLE_ADV_EVT_IDLE:
    // sleep_mode_enter();
    break;

  default:
    break;
  }
}

/**@brief Function for initializing the Advertising functionality.
 *
 * @details Encodes the required advertising data and passes it to the stack.
 *          Also builds a structure to be passed to the stack when starting advertising.
 */
static void advertising_init(void)
{
  ret_code_t err_code;
  ble_advertising_init_t init;

  memset(&init, 0, sizeof(init));

  init.advdata.name_type = BLE_ADVDATA_FULL_NAME;
  init.advdata.include_appearance = true;
  init.advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
  init.advdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
  init.advdata.uuids_complete.p_uuids = m_adv_uuids;

  init.config.ble_adv_fast_enabled = true;
  init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
  init.config.ble_adv_fast_timeout = APP_ADV_DURATION;

  init.evt_handler = on_adv_evt;

  err_code = ble_advertising_init(&m_advertising, &init);
  APP_ERROR_CHECK(err_code);

  ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

/**@brief Function for handling the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module that
 *          are passed to the application.
 *
 * @note All this function does is to disconnect. This could have been done by simply
 *       setting the disconnect_on_fail config parameter, but instead we use the event
 *       handler mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t *p_evt)
{
  ret_code_t err_code;

  if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
  {
    err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
    APP_ERROR_CHECK(err_code);
  }
}

/**@brief Function for handling a Connection Parameters error.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
  APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void)
{
  ret_code_t err_code;
  ble_conn_params_init_t cp_init;

  memset(&cp_init, 0, sizeof(cp_init));

  cp_init.p_conn_params = NULL;
  cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
  cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
  cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
  cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
  cp_init.disconnect_on_fail = false;
  cp_init.evt_handler = on_conn_params_evt;
  cp_init.error_handler = conn_params_error_handler;

  err_code = ble_conn_params_init(&cp_init);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling Peer Manager events.
 *
 * @param[in] p_evt  Peer Manager event.
 */
static void pm_evt_handler(pm_evt_t const *p_evt)
{
  ret_code_t err_code;

  pm_handler_on_pm_evt(p_evt);
  pm_handler_flash_clean(p_evt);

  switch (p_evt->evt_id)
  {
  case PM_EVT_PEERS_DELETE_SUCCEEDED:
    advertising_start(false);
    break;

  case PM_EVT_CONN_SEC_START:
    break;

  case PM_EVT_CONN_SEC_SUCCEEDED:
    // Update the rank of the peer.
    ble_conn_state_role(p_evt->conn_handle);
    break;

  case PM_EVT_CONN_SEC_FAILED:
    // In some cases, when securing fails, it can be restarted directly. Sometimes it can be
    // restarted, but only after changing some Security Parameters. Sometimes, it cannot be
    // restarted until the link is disconnected and reconnected. Sometimes it is impossible
    // to secure the link, or the peer device does not support it. How to handle this error
    // is highly application-dependent.
    m_conn_handle = BLE_CONN_HANDLE_INVALID;
    err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    // APP_ERROR_CHECK(err_code);
    break;

  case PM_EVT_CONN_SEC_CONFIG_REQ:
  {
    // A connected peer (central) is trying to pair, but the Peer Manager already has a bond
    // for that peer. Setting allow_repairing to false rejects the pairing request.
    // If this event is ignored (pm_conn_sec_config_reply is not called in the event
    // handler), the Peer Manager assumes allow_repairing to be false.
    pm_conn_sec_config_t conn_sec_config = {.allow_repairing = false};
    pm_conn_sec_config_reply(p_evt->conn_handle, &conn_sec_config);
  }
  break;

  case PM_EVT_STORAGE_FULL:
    // Run garbage collection on the flash.
    err_code = fds_gc();
    if (err_code == FDS_ERR_BUSY || err_code == FDS_ERR_NO_SPACE_IN_QUEUES)
    {
      // Retry.
    }
    else
    {
      APP_ERROR_CHECK(err_code);
    }
    break;

  default:
    break;
  }
}

/**@brief Function for the Peer Manager initialization.
 */
static void peer_manager_init(void)
{
  ble_gap_sec_params_t sec_param;
  ret_code_t err_code;

  err_code = pm_init();
  APP_ERROR_CHECK(err_code);

  memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));

  // Security parameters to be used for all security procedures.
  sec_param.bond = SEC_PARAM_BOND;
  sec_param.mitm = SEC_PARAM_MITM;
  sec_param.lesc = SEC_PARAM_LESC;
  sec_param.keypress = SEC_PARAM_KEYPRESS;
  sec_param.io_caps = SEC_PARAM_IO_CAPABILITIES;
  sec_param.oob = SEC_PARAM_OOB;
  sec_param.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
  sec_param.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
  sec_param.kdist_own.enc = 1;
  sec_param.kdist_own.id = 1;
  sec_param.kdist_peer.enc = 1;
  sec_param.kdist_peer.id = 1;

  err_code = pm_sec_params_set(&sec_param);
  APP_ERROR_CHECK(err_code);

  err_code = pm_register(pm_evt_handler);
  APP_ERROR_CHECK(err_code);
}

void ble_init(void)
{
  ble_stack_init();
  gap_params_init();
  gatt_init();
  services_init();
  advertising_init();
  conn_params_init();
  peer_manager_init();
  advertising_start(true);
}

static void init_app_timers(void)
{
  ret_code_t err_code;
  err_code = app_timer_init();
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_create(&main_timer, APP_TIMER_MODE_REPEATED, main_timer_timeout);
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_start(main_timer, MAIN_INTERVAL, NULL);
  APP_ERROR_CHECK(err_code);
}

int main(void)
{

  lfclk_config();
  init_app_timers();
  ret_code_t err_code = nrf_pwr_mgmt_init();
  APP_ERROR_CHECK(err_code);
  buttons_init();
  softdevice_setup();
  //read the flash memory and setup the ANT ID and Bluetooth flag
  eeprom_init(&old_ant_device_id, &enable_bluetooth);
  new_ant_device_id = old_ant_device_id; //no change at this time.
  profile_setup();                       // now set up the ant profile for ID 0

  if (enable_bluetooth)
    ble_init();

  while (1)
  {
    //user has not made an ant ID change in the last 10 minutes after long press of PLUS button
    if (ui32_seconds_since_startup > 600 && enable_bluetooth)
    {
      ui32_seconds_since_startup = 0; // turn off bluetooth after 10 min if left on

      eeprom_write_variables(old_ant_device_id, 0); // disable bluetooth and restart
    }
    // main timer calls main_timer_timeout every 10ms
    // check every second
    if (main_ticks % (1000 / MSEC_PER_TICK) == 0)
    {
      // first see if there was a change to the ANT ID, if so, store in flash and turn the bluetooth on restart
      if (new_ant_device_id != old_ant_device_id)
      {
        old_ant_device_id = new_ant_device_id;
        eeprom_write_variables(old_ant_device_id, 0); // DISABLE BLUETOOTH on restart
      }
      // bluetooth needs to be turned on by long press of the PLUS button
      if (turn_bluetooth_on)
        eeprom_write_variables(old_ant_device_id, 1); // Enable BLUETOOTH on restart
      if (turn_bluetooth_off)
        eeprom_write_variables(old_ant_device_id, 0); // Disable BLUETOOTH on restart
    }
  }
}
