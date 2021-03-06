/*
Copyright(c) 2016 Atmel Corporation, a wholly owned subsidiary of Microchip Technology Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http ://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
"""
*/

#ifdef CUPDI

#include "platform/platform.h"
#include "device/device.h"
#include "link.h"
#include "application.h"
#include "constants.h"

/*
    APP level memory struct
    @mgwd: magicword
    @link: pointer to link object
    @dev: point chip dev object
*/
typedef struct _upd_application {
#define UPD_APPLICATION_MAGIC_WORD 0xB4B4 //'uapp'
    unsigned int mgwd;  //magic word
    void *link;
    device_info_t *dev;
}upd_application_t;

/*
    Macro definition of APP level
    @VALID_APP(): check whether valid APP object
    @LINK(): get link object ptr
    @APP_REG(): chip reg address
*/
#define VALID_APP(_app) ((_app) && ((_app)->mgwd == UPD_APPLICATION_MAGIC_WORD))
#define LINK(_app) ((_app)->link)
#define APP_REG(_app, _name) ((_app)->dev->mmap->reg._name)

/*
    APP object init
    @port: serial port name of Window or Linux
    @baud: baudrate
    @dev: point chip dev object
    @return APP ptr, NULL if failed
*/
upd_application_t application;
void *updi_application_init(const char *port, int baud, void *dev)
{
    upd_application_t *app = NULL;
    void *link;

    DBG_INFO(APP_DEBUG, "<APP> init application");

    link = updi_datalink_init(port, baud);
    if (link) {
        app = &application;//(upd_application_t *)malloc(sizeof(*app));
        app->mgwd = UPD_APPLICATION_MAGIC_WORD;
        app->link = (void *)link;
        app->dev = (device_info_t *)dev;
    }

    return app;
}

/*
    APP object destroy
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @no return
*/
void updi_application_deinit(void *app_ptr)
{
    upd_application_t *app = (upd_application_t *)app_ptr;
    if (VALID_APP(app)) {
        DBG_INFO(APP_DEBUG, "<APP> deinit application");

        updi_datalink_deinit(LINK(app));
        //free(app);
    }
}

/*
    APP get device ID information, in Unlocked Mode, the SIGROW could be readout
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @return 0 successful, other value if failed
*/
int app_device_info(void *app_ptr)
{
    /*
        Reads out device information from various sources
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    u8 sib[16];
    //u8 pdi;
    u8 sigrow[14];
    u8 revid[1];
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Device info");

    result = link_read_sib(LINK(app), sib, sizeof(sib));
    if (result) {
        DBG_INFO(APP_DEBUG, "link_read_sib failed %d", result);
        return -2;
    }

    DBG(APP_DEBUG, "[SIB]", sib, sizeof(sib), (unsigned char *)"%02x ");
    DBG(APP_DEBUG, "[Family ID]", sib, 7, (unsigned char *)"%c");
    DBG(APP_DEBUG, "[NVM revision]", sib + 8, 3, (unsigned char *)"%c");
    DBG(APP_DEBUG, "[OCD revision]", sib + 11, 3, (unsigned char *)"%c");
    DBG_INFO(APP_DEBUG, "[PDI OSC] is %cMHz", sib[15]);

    //pdi = link_ldcs(LINK(app), UPDI_CS_STATUSA);
    DBG_INFO(APP_DEBUG, "[PDI Rev] is %d", (pdi >> 4));

    if (app_in_prog_mode(app)) {
        result = app_read_data(app, APP_REG(app, sigrow_address), sigrow, sizeof(sigrow));
        if (result) {
            DBG_INFO(APP_DEBUG, "app_read_data sigrow failed %d", result);
            return -3;
        }

        result = app_read_data(app, APP_REG(app, syscfg_address) + 1, revid, sizeof(revid));
        if (result) {
            DBG_INFO(APP_DEBUG, "app_read_data revid failed %d", result);
            return -4;
        }
        DBG(APP_DEBUG, "[Device ID]", sigrow, 3, (unsigned char *)"%02x ");
        DBG(APP_DEBUG, "[Sernum ID]", sigrow + 3, 10, (unsigned char *)"%02x ");
        DBG_INFO(APP_DEBUG, "[Device Rev] is %c", revid[0] + 'A');
    }

    return 0;
}

/*
    APP check whether device in Unlocked Mode
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @return true if Unlocked, other value if Locked
*/
bool app_in_prog_mode(void *app_ptr)
{
    /*
        Checks whether the NVM PROG flag is up
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    u8 status;
    int result;
    bool ret = false;

    if (!VALID_APP(app))
        return ret;

    result = _link_ldcs(LINK(app), UPDI_ASI_SYS_STATUS, &status);
    if (!result && status & (1 << UPDI_ASI_SYS_STATUS_NVMPROG))
        ret = true;

    DBG_INFO(APP_DEBUG, "<APP> In PROG mode: %d", ret);

    return ret;
}

/*
    APP waiting Unlocked completed
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @timeout: max waiting time
    @return 0 successful, other value if failed
*/
int app_wait_unlocked(void *app_ptr, int timeout)
{
    /*   
        Waits for the device to be unlocked.
        All devices boot up as locked until proven otherwise
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    u8 status;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Wait Unlock");

    do {
        result = _link_ldcs(LINK(app), UPDI_ASI_SYS_STATUS, &status);
        if (result) {
            DBG_INFO(APP_DEBUG, "_link_ldcs failed %d", result);
        }
        else {
            if (!(status & (1 << UPDI_ASI_SYS_STATUS_LOCKSTATUS)))
                break;
        }

        msleep(1);
    } while (--timeout > 0);

    if (timeout <= 0 || result) {
        DBG_INFO(APP_DEBUG, "Timeout waiting for device to unlock status %02x result %d", status, result);
        return -2;
    }

    return 0;
}

/*
    APP Unlocked chip by command UPDI_KEY_CHIPERASE, this will fully erase whole chip
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @return 0 successful, other value if failed
*/
int app_unlock(void *app_ptr)
{
    /*
        Unlock and erase
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    u8 status;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> unlock");

    // Put in the key
    result = link_key(LINK(app), UPDI_KEY_64, UPDI_KEY_CHIPERASE);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_key failed %d", result);
        return -2;
    }

    // Check key status
    result = _link_ldcs(LINK(app), UPDI_ASI_KEY_STATUS, &status);
    if (result || !(status & (1 << UPDI_ASI_KEY_STATUS_CHIPERASE))) {
        DBG_INFO(APP_DEBUG, "_link_ldcs Chiperase Key not accepted(%d), status 0x%02x", result, status);
        return -3;
    }

    //Toggle reset
    result = app_toggle_reset(app_ptr, 1);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_toggle_reset failed %d", result);
        return -4;
    }

    //And wait for unlock
    result = app_wait_unlocked(app, 100);
    if (result) {
        DBG_INFO(APP_DEBUG, "Failed to chip erase using key result %d", result);
        return -5;
    }
    
    return 0;
}

/*
    APP Unlocked chip by command UPDI_KEY_NVM
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @return 0 successful, other value if failed
*/
int app_enter_progmode(void *app_ptr)
{
    /*
        Enters into NVM programming mode
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    u8 status;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Enter Progmode");

    // First check if NVM is already enabled
    if (app_in_prog_mode(app_ptr)) {
        DBG_INFO(APP_DEBUG, "Already in NVM programming mode");
        return 0;
    }

    DBG_INFO(APP_DEBUG, "Entering NVM programming mode");

    // Put in the key
    result = link_key(LINK(app), UPDI_KEY_64, UPDI_KEY_NVM);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_key failed %d", result);
        return -2;
    }

    // Check key status
    result = _link_ldcs(LINK(app), UPDI_ASI_KEY_STATUS, &status);
    if (result || !(status & (1 << UPDI_ASI_KEY_STATUS_NVMPROG))) {
        DBG_INFO(APP_DEBUG, "_link_ldcs Nvm Key not accepted(%d), status 0x%02x", result, status);
        return -3;
    }

    //Toggle reset
    result = app_toggle_reset(app_ptr, 1);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_toggle_reset failed %d", result);
        return -4;
    }

    //And wait for unlock
    result = app_wait_unlocked(app_ptr, 100);
    if (result) {
        DBG_INFO(APP_DEBUG, "Failed to enter NVM programming mode: device is locked result %d", result);
        return -5;
    }

    if (!app_in_prog_mode(app_ptr)) {
        DBG_INFO(APP_DEBUG, "Failed to enter NVM programming mode");
        return -6;
    }else {
        DBG_INFO(APP_DEBUG, "Now in NVM programming mode");
        return 0;
    }
}

/*
    APP Leave Unlocked mode and re-lock it
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @return 0 successful, other value if failed
*/
int app_leave_progmode(void *app_ptr)
{
    /*
        Disables UPDI which releases any keys enabled
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Leaving program mode");

    result = app_toggle_reset(app_ptr, 1);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_toggle_reset failed %d", result);
        return -2;
    }

    result = app_disable(app_ptr);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_disable failed %d", result);
        return -3;
    }

    return 0;
}

/*
APP disable updi interface
@app_ptr: APP object pointer, acquired from updi_application_init()
@return 0 successful, other value if failed
*/
int app_disable(void *app_ptr)
{
    /*
    Disable UPDI interface temperarily
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Disable");

    result = link_stcs(LINK(app), UPDI_CS_CTRLB, (1 << UPDI_CTRLB_UPDIDIS_BIT) | (1 << UPDI_CTRLB_CCDETDIS_BIT));
    if (result) {
        DBG_INFO(APP_DEBUG, "link_stcs failed %d", result);
        return -2;
    }

    return 0;
}

/*
    APP send chip reset
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @apply_reset: true - set, false - clear
    @return 0 successful, other value if failed
*/
int app_reset(void *app_ptr, bool apply_reset)
{
    /*
    Applies or releases an UPDI reset condition
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Reset %d", apply_reset);

    if (apply_reset) {
        DBG_INFO(APP_DEBUG, "Apply reset");
        result = link_stcs(LINK(app), UPDI_ASI_RESET_REQ, UPDI_RESET_REQ_VALUE);
    }
    else {
        DBG_INFO(APP_DEBUG, "Release reset");
        result = link_stcs(LINK(app), UPDI_ASI_RESET_REQ, 0);
    }

    if (result) {
        DBG_INFO(APP_DEBUG, "link_stcs failed %d", result);
        return -2;
    }

    return 0;
}

/*
    APP toggle a chip reset
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @delay: Reset keep time before clear
    @return 0 successful, other value if failed
*/
int app_toggle_reset(void *app_ptr, int delay)
{
    /*
    Toggle an UPDI reset condition
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Toggle Reset");

    //Toggle reset
    result = app_reset(app, true);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_reset failed %d", result);
        return -2;
    }

    msleep(delay);

    result = app_reset(app, false);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_reset failed %d", result);
        return -3;
    }

    return 0;
}

/*
    APP wait flash ready
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @timeout: max flash programing time
    @return 0 successful, other value if failed
*/
int app_wait_flash_ready(void *app_ptr, int timeout)
{
    /*
        Waits for the NVM controller to be ready
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    u8 status;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Wait flash ready");

    do {
        result = _link_ld(LINK(app), APP_REG(app, nvmctrl_address) + UPDI_NVMCTRL_STATUS, &status);
        if (result) {
            DBG_INFO(APP_DEBUG, "_link_ld failed %d", result);
            result = -2;
            break;
        }
        else {
            if (status & (1 << UPDI_NVM_STATUS_WRITE_ERROR)) {
                result = -3;
                break;
            }

            if (!(status & ((1 << UPDI_NVM_STATUS_EEPROM_BUSY) | (1 << UPDI_NVM_STATUS_FLASH_BUSY))))
                break;
        }

        msleep(1);
    } while (--timeout > 0);

    if (timeout <= 0 || result) {
        DBG_INFO(APP_DEBUG, "Timeout waiting for wait flash ready status %02x result %d", status, result);
        return -3;
    }

    return 0;
}

/*
    APP send a nvm command
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @command: command content
    @return 0 successful, other value if failed
*/
int app_execute_nvm_command(void *app_ptr, u8 command)
{
    /*
        Executes an NVM COMMAND on the NVM CTRL
    */
    upd_application_t *app = (upd_application_t *)app_ptr;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> NVMCMD %d executing", command);

    return link_st(LINK(app), APP_REG(app, nvmctrl_address) + UPDI_NVMCTRL_CTRLA, command);
}

/*
    APP erase page
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @page: page address to be erased
    @return 0 successful, other value if failed
*/
int app_page_erase(void *app_ptr, u16 address)
{
    /*
    Does a chip erase using the NVM controller
    Note that on locked devices this it not possible and the ERASE KEY has to be used instead
    */

    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> page erase using NVM CTRL");

    //Wait until NVM CTRL is ready to erase
    result = app_wait_flash_ready(app, TIMEOUT_WAIT_FLASH_READY);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_wait_flash_ready timeout before erase failed %d", result);
        return -2;
    }

    //Erase
    result = app_execute_nvm_command(app, UPDI_NVMCTRL_CTRLA_CHIP_ERASE);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_execute_nvm_command failed %d", result);
        return -3;
    }

    // And wait for it
    result = app_wait_flash_ready(app, TIMEOUT_WAIT_FLASH_READY);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_wait_flash_ready timeout after erase failed %d", result);
        return -2;
    }

    return 0;
}

/*
    APP erase chip
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @return 0 successful, other value if failed
*/
int app_chip_erase(void *app_ptr)
{
    /*
        Does a chip erase using the NVM controller
        Note that on locked devices this it not possible and the ERASE KEY has to be used instead
    */

    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Chip erase using NVM CTRL");

    //Wait until NVM CTRL is ready to erase
    result = app_wait_flash_ready(app, TIMEOUT_WAIT_FLASH_READY);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_wait_flash_ready timeout before erase failed %d", result);
        return -2;
    }

    //Erase
    result = app_execute_nvm_command(app, UPDI_NVMCTRL_CTRLA_CHIP_ERASE);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_execute_nvm_command failed %d", result);
        return -3;
    }

    // And wait for it
    result = app_wait_flash_ready(app, TIMEOUT_WAIT_FLASH_READY);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_wait_flash_ready timeout after erase failed %d", result);
        return -2;
    }

    return 0;
}

/*
    APP read data in 16bit mode
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data output buffer
    @len: data len
    @return 0 successful, other value if failed
*/
int app_read_data_words(void *app_ptr, u16 address, u8 *data, int len)
{
    /*
    Reads a number of words of data from UPDI
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app) || !VALID_PTR(data) || len < 2)
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Read words data(%d) addr: %hX", len, address);

    // Special-case of 1 word
    if (len == 2) {
        result = _link_ld16(LINK(app), address, (u16 *)data);
        if (result) {
            DBG_INFO(APP_DEBUG, "_link_ld16 failed %d", result);
            return -2;
        }

        return 0;
    }

    // Range check
    if (len > (UPDI_MAX_REPEAT_SIZE >> 1) + 1) {
        DBG_INFO(APP_DEBUG, "Read data length out of size %d", len);
        return -3;
    }

    // Store the address
    result = link_st_ptr(LINK(app), address);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_st_ptr failed %d", result);
        return -4;
    }

    //Fire up the repeat
    result = link_repeat16(LINK(app), (len >> 1) - 1);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_repeat16 failed %d", result);
        return -5;
    }

    //Do the read(s)
    result = link_ld_ptr_inc16(LINK(app), data, len);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_ld_ptr_inc16 failed %d", result);
        return -6;
    }

    return 0;
}

/*
    APP read data in 8bit mode
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data output buffer
    @len: data len
    @return 0 successful, other value if failed
*/
int app_read_data_bytes(void *app_ptr, u16 address, u8 *data, int len)
{
    /*
    Reads a number of bytes of data from UPDI
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app) || !VALID_PTR(data) || len < 1)
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Read bytes data(%d) addr: %hX", len, address);

    // Special-case of 1 byte
    if (len == 1) {
        result = _link_ld(LINK(app), address, data);
        if (result) {
            DBG_INFO(APP_DEBUG, "_link_ld failed %d", result);
            return -2;
        }

        return 0;
    }

    // Range check
    if (len > UPDI_MAX_REPEAT_SIZE + 1) {
        DBG_INFO(APP_DEBUG, "Read data length out of size %d", len);
        return -3;
    }

    // Store the address
    result = link_st_ptr(LINK(app), address);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_st_ptr failed %d", result);
        return -4;
    }

    //Fire up the repeat
    result = link_repeat(LINK(app), len - 1);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_repeat failed %d", result);
        return -5;
    }

    //Do the read(s)
    result = link_ld_ptr_inc(LINK(app), data, len);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_ld_ptr_inc failed %d", result);
        return -6;
    }

    return 0;
}

/*
    APP read data with 8/16 bit auto select by len
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data output buffer
    @len: data len
    @return 0 successful, other value if failed
*/
int app_read_data(void *app_ptr, u16 address, u8 *data, int len)
{
    /*
    Reads a number of bytes of data from UPDI
    */
    bool use_word_access = !(len & 0x1);
    int result;

    DBG_INFO(APP_DEBUG, "<APP> Read data(%d)", len);

    if (!VALID_PTR(data) || len <= 0)
        return ERROR_PTR;

    if (use_word_access)
        result = app_read_data_words(app_ptr, address, data, len);
    else
        result = app_read_data_bytes(app_ptr, address, data, len);

    return result;
}

/*
    APP read flash
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data output buffer
    @len: data len
    @return 0 successful, other value if failed
*/
int app_read_nvm(void *app_ptr, u16 address, u8 *data, int len)
{
    /*
    Read data from NVM.
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Chip read nvm");

    // Load to buffer by reading directly to location
    result = app_read_data(app, address, data, len);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_read_data failed %d", result);
        return -2;
    }

    return 0;
}

/*
    APP write data in 16bit mode
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data buffer
    @len: data len
    @return 0 successful, other value if failed
*/
int app_write_data_words(void *app_ptr, u16 address, const u8 *data, int len)
{
    /*
        Writes a number of words to memory
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app) || !VALID_PTR(data) || len < 2)
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Write words data(%d) addr: %hX", len, address);
    
    // Special-case of 1 word
    if (len == 2) {
        result = link_st16(LINK(app), address, data[0] + (data[1] << 8));
        if (result) {
            DBG_INFO(APP_DEBUG, "link_st16 failed %d", result);
            return -3;
        }

        return 0;
    }

    // Range check
    if (len > ((UPDI_MAX_REPEAT_SIZE + 1) << 1)) {
        DBG_INFO(APP_DEBUG, "Write words data length out of size %d", len);
        return -3;
    }

    // Store the address
    result = link_st_ptr(LINK(app), address);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_st_ptr failed %d", result);
        return -4;
    }

    //Fire up the repeat
    result = link_repeat16(LINK(app), (len >> 1) - 1);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_repeat16 failed %d", result);
        return -5;
    }

    result = link_st_ptr_inc16(LINK(app), data, len);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_st_ptr_inc16 failed %d", result);
        return -6;
    }

    return 0;
}

/*
    APP write data in 8bit mode
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data buffer
    @len: data len
    @return 0 successful, other value if failed
*/
int app_write_data_bytes(void *app_ptr, u16 address, const u8 *data, int len)
{
    /*
    Writes a number of bytes to memory
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app) || !VALID_PTR(data) || len < 1)
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Write bytes data(%d) addr: %hX", len, address);

    // Special-case of 1 byte
    if (len == 1) {
        result = link_st(LINK(app), address, data[0]);
        if (result) {
            DBG_INFO(APP_DEBUG, "link_st16 failed %d", result);
            return -2;
        }
    }

    // Range check
    if (len > UPDI_MAX_REPEAT_SIZE + 1) {
        DBG_INFO(APP_DEBUG, "Write data length out of size %d", len);
        return -3;
    }

    // Store the address
    result = link_st_ptr(LINK(app), address);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_st_ptr failed %d", result);
        return -4;
    }

    //Fire up the repeat
    result = link_repeat(LINK(app), len - 1);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_repeat failed %d", result);
        return -5;
    }
 
    result = link_st_ptr_inc(LINK(app), data, len);
    if (result) {
        DBG_INFO(APP_DEBUG, "link_st_ptr_inc16 failed %d", result);
        return -6;
    }

    return 0;
}

/*
    APP write data with 8/16 bit auto select by len
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data buffer
    @len: data len
    @use_word_access: whether use 2 bytes mode for writing
    @return 0 successful, other value if failed
*/
int app_write_data(void *app_ptr, u16 address, const u8 *data, int len, bool use_word_access)
{
    /*
    Writes a number of data to memory
    */
    int result;

    DBG_INFO(APP_DEBUG, "<APP> Write data(%d)", len);

    if (!VALID_PTR(data) || len <= 0)
        return ERROR_PTR;

    if (use_word_access)
        result = app_write_data_words(app_ptr, address, data, len);
    else
        result = app_write_data_bytes(app_ptr, address, data, len);
    
    return result;
}

/*
    APP write nvm
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data buffer
    @len: data len
    @nvm_command: programming command
    @return 0 successful, other value if failed
*/
int _app_write_nvm(void *app_ptr, u16 address, const u8 *data, int len, u8 nvm_command, bool use_word_access)
{
    /*
        Writes a page of data to NVM.
        By default the PAGE_WRITE command is used, which requires that the page is already erased.
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int result;

    if (!VALID_APP(app))
        return ERROR_PTR;

    DBG_INFO(APP_DEBUG, "<APP> Chip write nvm");

    // Check that NVM controller is ready
    result = app_wait_flash_ready(app, TIMEOUT_WAIT_FLASH_READY);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_wait_flash_ready timeout before page buffer clear failed %d", result);
        return -2;
    }

    // Erase write command will clear the buffer automantic
    
    //Clear the page buffer
    DBG_INFO(APP_DEBUG, "Clear page buffer");
    result = app_execute_nvm_command(app, UPDI_NVMCTRL_CTRLA_PAGE_BUFFER_CLR);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_execute_nvm_command failed %d", UPDI_NVMCTRL_CTRLA_PAGE_BUFFER_CLR, result);
        return -3;
    }

    // Waif for NVM controller to be ready
    result = app_wait_flash_ready(app, TIMEOUT_WAIT_FLASH_READY);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_wait_flash_ready timeout after page buffer clear failed %d", result);
        return -4;
    }
    
    // Load the page buffer by writing directly to location
    result = app_write_data(app, address, data, len, use_word_access);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_write_data failed %d", result);
        return -5;
    }

    // Write the page to NVM, maybe erase first
    DBG_INFO(APP_DEBUG, "Committing page");
    result = app_execute_nvm_command(app, nvm_command);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_execute_nvm_command(%d) failed %d", nvm_command, result);
        return -6;
    }

    // Waif for NVM controller to be ready again
    result = app_wait_flash_ready(app, TIMEOUT_WAIT_FLASH_READY);
    if (result) {
        DBG_INFO(APP_DEBUG, "app_wait_flash_ready timeout after page write failed %d", result);
        return -7;
    }

    return 0;
}

/*
    APP write nvm capsule with UPDI_NVMCTRL_CTRLA_WRITE_PAGE command
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data buffer
    @len: data len
    @return 0 successful, other value if failed
*/
int app_write_nvm(void *app_ptr, u16 address, const u8 *data, int len)
{
    bool use_word_access = !(len & 0x1);

    return _app_write_nvm(app_ptr, address, data, len, UPDI_NVMCTRL_CTRLA_WRITE_PAGE, use_word_access);
}

/*
    APP write flash capsule with UPDI_NVMCTRL_CTRLA_ERASE_WRITE_PAGE command
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data buffer
    @len: data len
    @use_word_access: 2 bytes mode for writting
    @return 0 successful, other value if failed
*/
/*int _app_erase_write_nvm(void *app_ptr, u16 address, const u8 *data, int len, bool use_word_access)
{
    return _app_write_nvm(app_ptr, address, data, len, UPDI_NVMCTRL_CTRLA_ERASE_WRITE_PAGE, use_word_access);
}*/

/*
    APP write flash capsule with UPDI_NVMCTRL_CTRLA_ERASE_WRITE_PAGE command, and determine whether use 2 byte for writting
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data buffer
    @len: data len
    @return 0 successful, other value if failed
*/
/*int app_erase_write_nvm(void *app_ptr, u16 address, const u8 *data, int len)
{
    bool use_word_access = !(len & 0x1);

    return _app_write_nvm(app_ptr, address, data, len, UPDI_NVMCTRL_CTRLA_ERASE_WRITE_PAGE, use_word_access);
}*/
/*
    APP load register value
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data buffer
    @len: data len
    @return 0 successful, other value if failed
*/
#if 0
int app_ld_reg(void *app_ptr, u16 address, u8* data, int len)
{
    /*
        Load reg data
    */
    upd_application_t *app = (upd_application_t *)app_ptr;
    int i, result;

    for (i = 0; i < len; i++) {
        result = _link_ld(LINK(app), address + i, data + i);
        if (result) {
            DBG_INFO(APP_DEBUG, "_link_ld(%x) +%d failed %d", address, i, result);
            return -2;
        }
    }

    return 0;
}

/*
    APP set register value
    @app_ptr: APP object pointer, acquired from updi_application_init()
    @address: target address
    @data: data buffer
    @len: data len
    @return 0 successful, other value if failed
*/
int app_st_reg(void *app_ptr, u16 address, const u8 *data, int len)
{
    /*
        Set reg data
    */

    upd_application_t *app = (upd_application_t *)app_ptr;
    int i, result;

    for (i = 0; i < len; i++) {
        result = link_st(LINK(app), address + i, data[i]);
        if (result) {
            DBG_INFO(APP_DEBUG, "link_st(%x) +%d failed %d", address, i, result);
            return -2;
        }
    }

    return 0;
}
#endif

#endif