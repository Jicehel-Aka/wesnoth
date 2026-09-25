/*
This file is part of the Gamebuino-AKA library,
Copyright (c) Gamebuino 2026

This is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License (LGPL)
as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version.

This is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License (LGPL) for more details.

You should have received a copy of the GNU Lesser General Public
License (LGPL) along with the library.
If not, see <http://www.gnu.org/licenses/>.

Authors:
 - Jean-Marie Papillon
*/
#include "driver/i2c_master.h"
#include "gb_common.h"
#include "gb_ll_system.h"

    // hold current expander status to allow change a bit without reloading all
uint8_t u8_expander_out_data = 0;


#define I2C_PORT_NUM_0 ((i2c_port_num_t)0)
i2c_master_bus_config_t i2c_mst_config = {
    .i2c_port = I2C_NUM_0    ,
    .sda_io_num = EXPANDER_I2C_SDA,
    .scl_io_num = EXPANDER_I2C_SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};

i2c_master_bus_handle_t bus_handle;

i2c_device_config_t dev_cfg0 = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = EXPANDER_I2C_ADDRESS0,
    .scl_speed_hz = 400000,
};
i2c_master_dev_handle_t dev_handle0;

i2c_device_config_t dev_cfg1 = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = EXPANDER_I2C_ADDRESS1,
    .scl_speed_hz = 400000,
};
i2c_master_dev_handle_t dev_handle1;

i2c_device_config_t dev_cfg_audio = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = AUDIO_AMP_I2C_ADDRESS,
    .scl_speed_hz = 400000,
};
i2c_master_dev_handle_t dev_handle_audio;

void gb_ll_expander_write(uint8_t u8_data)
{
    u8_expander_out_data = u8_data |= EXPANDER_KEY; // Key inputs must be stay HIGH
    esp_err_t ret = i2c_master_transmit(dev_handle0, &u8_expander_out_data, sizeof(u8_expander_out_data), 100);
    if(ret)
        printf( "I2C wite return %d\n", ret );
}


uint16_t gb_ll_expander_read()
{
    uint8_t u8_d1 = 0x55;
    esp_err_t ret1 = i2c_master_receive(dev_handle1, &u8_d1, sizeof(u8_d1), 100);
    uint8_t u8_d0 = 0x55;
    esp_err_t ret0 = i2c_master_receive(dev_handle0, &u8_d0, sizeof(u8_d0), 100);
    uint16_t u16_data = u8_d0 + 256*(uint16_t)u8_d1;
    u16_data ^= EXPANDER_KEY_RUN; // Run key active high => active low
    u16_data ^= EXPANDER_KEY;     // all key active low => active high
    if ( ret0 || ret1 )
    {
        printf( "I2C read return %d %d\n", ret0, ret1 );
        return 0;
    }
//    printf( "I2C read return %d %d 0x%04X\n", ret0, ret1, u16_data );
    return u16_data;
}


void gb_ll_audio_amp_write(uint8_t u8_reg, uint8_t u8_data)
{
    uint8_t u8_datareg[2] = { u8_reg, u8_data };
//    u8_data |= EXPANDER_KEY; // Key inputs must be stay HIGH
    esp_err_t ret = i2c_master_transmit( dev_handle_audio, u8_datareg, sizeof(u8_datareg), 100 );
    if(ret)
        printf( "I2C Audio wite return %d\n", ret );
}

uint8_t gb_ll_audio_amp_read( uint8_t u8_reg )
{
    uint8_t u8_data = 0;
    esp_err_t ret = i2c_master_transmit_receive( dev_handle_audio, &u8_reg, sizeof(u8_reg), &u8_data, sizeof(u8_data), 100 );
    if ( ret )
    {
        printf( "ERR : I2C Audio read return %d\n", ret );
        return 0;
    }
    printf( "I2C Audio read reg 0x%02X return 0x%02x\n", u8_reg, u8_data );
    return u8_data;
}

int gb_ll_i2c_init()
{
    printf( "Initialize I2C master bus... " ); 
    esp_err_t ret = i2c_new_master_bus(&i2c_mst_config, &bus_handle);
    if(ret)
        printf( "i2c_new_master_bus return %d\n", ret );

//    printf( "Initialize I2C Slave 0... " );
    esp_err_t ret0 = i2c_master_bus_add_device(bus_handle, &dev_cfg0, &dev_handle0);
    if ( ret0 )
        printf( "i2c_master_bus_add_device return %d\n", ret0 );
    
//    printf( "Initialize I2C Slave 1... " );
    esp_err_t ret1 = i2c_master_bus_add_device(bus_handle, &dev_cfg1, &dev_handle1);
    if ( ret1) 
        printf( "i2c_master_bus_add_device return %d\n", ret1 );

//    printf( "Initialize I2C Audio... " );
    esp_err_t ret2 = i2c_master_bus_add_device(bus_handle, &dev_cfg_audio, &dev_handle_audio);
    if (ret2)
        printf( "i2c_master_bus_add_device return %d\n", ret2 );

    if ( ret | ret0 | ret1 | ret2 )
        return -1; // fail
    return 0;
}