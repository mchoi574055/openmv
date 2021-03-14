/*
 * This file is part of the OpenMV project.
 *
 * Copyright (c) 2013-2021 Ibrahim Abdelkader <iabdalkader@openmv.io>
 * Copyright (c) 2013-2021 Kwabena W. Agyeman <kwagyeman@openmv.io>
 *
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * MT9V034 driver.
 */
#include "omv_boardconfig.h"
#if (OMV_ENABLE_MT9V034 == 1)

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cambus.h"
#include "sensor.h"
#include "mt9v034.h"
#include "mt9v034_regs.h"
#include "py/mphal.h"

#define ACTIVE_SENSOR_WIDTH     (752)
#define ACTIVE_SENSOR_HEIGHT    (480)

static int16_t readout_x = 0;
static int16_t readout_y = 0;

static int reset(sensor_t *sensor)
{
    int ret = 0;
    readout_x = 0;
    readout_y = 0;

    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_RESET, MT9V034_RESET_SOFT_RESET);

    uint16_t chip_control;
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_CHIP_CONTROL, &chip_control);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_CHIP_CONTROL,
            (chip_control & (~MT9V034_CHIP_CONTROL_RESERVED)));

    uint16_t read_mode;
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE, &read_mode);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE,
            read_mode | MT9V034_READ_MODE_ROW_FLIP | MT9V034_READ_MODE_COL_FLIP);

    // We have to copy the differences from context A into context B registers so that we can
    // ping-pong between them seamlessly...

    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE_B, &read_mode);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE_B,
            read_mode | MT9V034_READ_MODE_ROW_FLIP | MT9V034_READ_MODE_COL_FLIP);

    uint16_t shutter_width1;
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_SHUTTER_WIDTH1, &shutter_width1);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_SHUTTER_WIDTH1_B, shutter_width1);

    uint16_t shutter_width2;
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_SHUTTER_WIDTH2, &shutter_width2);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_SHUTTER_WIDTH2_B, shutter_width2);

    uint16_t shutter_control;
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_SHUTTER_WIDTH_CONTROL, &shutter_control);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_SHUTTER_WIDTH_CONTROL_B, shutter_control);

    uint16_t voltage_level_1;
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_V1_CONTROL, &voltage_level_1);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_V1_CONTROL_B, voltage_level_1);

    uint16_t voltage_level_2;
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_V2_CONTROL, &voltage_level_2);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_V2_CONTROL_B, voltage_level_2);

    uint16_t voltage_level_3;
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_V3_CONTROL, &voltage_level_3);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_V3_CONTROL_B, voltage_level_3);

    uint16_t voltage_level_4;
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_V4_CONTROL, &voltage_level_4);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_V4_CONTROL_B, voltage_level_4);

    uint16_t analog_gain;
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_ANALOG_GAIN, &analog_gain);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_ANALOG_GAIN_B, analog_gain);

    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_PIXEL_OPERATION_MODE,
            0);

    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_ADC_COMPANDING_MODE,
            MT9V034_ADC_COMPANDING_MODE_LINEAR | MT9V034_ADC_COMPANDING_MODE_LINEAR_B);

    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_AEC_AGC_ENABLE,
            MT9V034_AEC_ENABLE | MT9V034_AEC_ENABLE_B | MT9V034_AGC_ENABLE | MT9V034_AGC_ENABLE_B);

    return ret;
}

static int read_reg(sensor_t *sensor, uint16_t reg_addr)
{
    uint16_t reg_data;
    if (cambus_readw(&sensor->bus, sensor->slv_addr, reg_addr, &reg_data) != 0) {
        return -1;
    }
    return reg_data;
}

static int write_reg(sensor_t *sensor, uint16_t reg_addr, uint16_t reg_data)
{
    return cambus_writew(&sensor->bus, sensor->slv_addr, reg_addr, reg_data);
}

static int set_pixformat(sensor_t *sensor, pixformat_t pixformat)
{
    if (pixformat != PIXFORMAT_GRAYSCALE) {
        return -1;
    }

    return 0;
}

static int set_framesize(sensor_t *sensor, framesize_t framesize)
{
    uint16_t chip_control, read_mode;
    int ret = 0;
    uint16_t w = resolution[framesize][0];
    uint16_t h = resolution[framesize][1];

    if ((w > ACTIVE_SENSOR_WIDTH) || (h > ACTIVE_SENSOR_HEIGHT)) {
        return -1;
    }

    if (cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_CHIP_CONTROL, &chip_control) != 0) {
        return -1;
    }

    // EDIT: WORKS BETTER TO STAY IN CONTEXT A
    //
    // Determine which context to switch to...
    int context = 1; // chip_control & MT9V034_CHIP_CONTROL_CONTEXT;
    int read_mode_addr = context ? MT9V034_READ_MODE : MT9V034_READ_MODE_B;
    int col_start_addr = context ? MT9V034_COL_START : MT9V034_COL_START_B;
    int row_start_addr = context ? MT9V034_ROW_START : MT9V034_ROW_START_B;
    int window_height_addr = context ? MT9V034_WINDOW_HEIGHT : MT9V034_WINDOW_HEIGHT_B;
    int window_width_addr = context ? MT9V034_WINDOW_WIDTH : MT9V034_WINDOW_WIDTH_B;
    int horizontal_blanking_addr = context ? MT9V034_HORIZONTAL_BLANKING : MT9V034_HORIZONTAL_BLANKING_B;

    if (cambus_readw(&sensor->bus, sensor->slv_addr, read_mode_addr, &read_mode) != 0) {
        return -1;
    }

    int read_mode_mul = 1;
    read_mode &= 0xFFF0;

    if ((w <= (ACTIVE_SENSOR_WIDTH / 4)) && (h <= (ACTIVE_SENSOR_HEIGHT / 4))) {
        read_mode_mul = 4;
        read_mode |= MT9V034_READ_MODE_COL_BIN_4 | MT9V034_READ_MODE_ROW_BIN_4;
    } else if ((w <= (ACTIVE_SENSOR_WIDTH / 2)) && (h <= (ACTIVE_SENSOR_HEIGHT / 2))) {
        read_mode_mul = 2;
        read_mode |= MT9V034_READ_MODE_COL_BIN_2 | MT9V034_READ_MODE_ROW_BIN_2;
    }

    int readout_x_max = (ACTIVE_SENSOR_WIDTH - (w * read_mode_mul)) / 2;
    int readout_y_max = (ACTIVE_SENSOR_HEIGHT - (h * read_mode_mul)) / 2;
    readout_x = IM_MAX(IM_MIN(readout_x, readout_x_max), -readout_x_max);
    readout_y = IM_MAX(IM_MIN(readout_y, readout_y_max), -readout_y_max);

    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, col_start_addr,
            readout_x_max - readout_x + MT9V034_COL_START_MIN); // sensor is mirrored by default
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, row_start_addr,
            readout_y_max - readout_y + MT9V034_ROW_START_MIN); // sensor is mirrored by default
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, window_width_addr, w * read_mode_mul);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, window_height_addr, h * read_mode_mul);

    // Notes: 1. The MT9V034 uses column parallel analog-digital converters, thus short row timing is not possible.
    // The minimum total row time is 690 columns (horizontal width + horizontal blanking). The minimum
    // horizontal blanking is 61. When the window width is set below 627, horizontal blanking
    // must be increased.
    //
    // The STM32H7 needs more than 94+(752-640) clocks between rows otherwise it can't keep up with the pixel rate.
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, horizontal_blanking_addr,
            MT9V034_HORIZONTAL_BLANKING_DEF + (ACTIVE_SENSOR_WIDTH - IM_MIN(w * read_mode_mul, 640)));

    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, read_mode_addr, read_mode);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_PIXEL_COUNT, (w * h) / 8);

    // We need more setup time for the pixel_clk at the full data rate...
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_PIXEL_CLOCK,
            (read_mode_mul == 1) ? MT9V034_PIXEL_CLOCK_INV_PXL_CLK : 0);

    // EDIT: WORKS BETTER TO STAY IN CONTEXT A
    //
    // Flip the context.
    // ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_CHIP_CONTROL,
    //         chip_control ^ MT9V034_CHIP_CONTROL_CONTEXT);

    return ret;
}

static int set_colorbar(sensor_t *sensor, int enable)
{
    uint16_t test;
    int ret = cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_TEST_PATTERN, &test);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_TEST_PATTERN,
            (test & (~(MT9V034_TEST_PATTERN_ENABLE | MT9V034_TEST_PATTERN_GRAY_MASK)))
          | ((enable != 0) ? (MT9V034_TEST_PATTERN_ENABLE | MT9V034_TEST_PATTERN_GRAY_VERTICAL) : 0));
    ret |= sensor->snapshot(sensor, NULL, NULL); // Force shadow mode register to update...
    return ret;
}

static int set_auto_gain(sensor_t *sensor, int enable, float gain_db, float gain_db_ceiling)
{
    uint16_t reg;
    int ret = cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_AEC_AGC_ENABLE, &reg);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_AEC_AGC_ENABLE,
            (reg & (~(MT9V034_AGC_ENABLE | MT9V034_AGC_ENABLE_B))) |
            ((enable != 0) ? (MT9V034_AGC_ENABLE | MT9V034_AGC_ENABLE_B) : 0));
    ret |= sensor->snapshot(sensor, NULL, NULL); // Force shadow mode register to update...

    if ((enable == 0) && (!isnanf(gain_db)) && (!isinff(gain_db))) {
        int gain = IM_MAX(IM_MIN(fast_roundf(fast_expf((gain_db / 20.0f) * fast_log(10.0f)) * 16.0f), 64), 16);

        ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_ANALOG_GAIN, &reg);
        ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_ANALOG_GAIN, (reg & 0xFF80) | gain);
        ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_ANALOG_GAIN_B, &reg);
        ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_ANALOG_GAIN_B, (reg & 0xFF80) | gain);
    } else if ((enable != 0) && (!isnanf(gain_db_ceiling)) && (!isinff(gain_db_ceiling))) {
        int gain_ceiling = IM_MAX(IM_MIN(fast_roundf(fast_expf((gain_db_ceiling / 20.0f) * fast_log(10.0f)) * 16.0f), 64), 16);

        ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_MAX_GAIN, &reg);
        ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_MAX_GAIN, (reg & 0xFF80) | gain_ceiling);
    }

    return ret;
}

static int get_gain_db(sensor_t *sensor, float *gain_db)
{
    uint16_t gain;
    int ret = cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_ANALOG_GAIN, &gain);
    *gain_db = 20.0 * (fast_log((gain & 0x7F) / 16.0f) / fast_log(10.0f));
    return ret;
}

static int set_auto_exposure(sensor_t *sensor, int enable, int exposure_us)
{
    uint16_t reg, row_time_0, row_time_1;
    int ret = cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_AEC_AGC_ENABLE, &reg);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_AEC_AGC_ENABLE,
            (reg & (~(MT9V034_AEC_ENABLE | MT9V034_AEC_ENABLE_B))) |
            ((enable != 0) ? (MT9V034_AEC_ENABLE | MT9V034_AEC_ENABLE_B) : 0));
    ret |= sensor->snapshot(sensor, NULL, NULL); // Force shadow mode register to update...

    if ((enable == 0) && (exposure_us >= 0)) {
        ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_WINDOW_WIDTH, &row_time_0);
        ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_HORIZONTAL_BLANKING, &row_time_1);

        int exposure = IM_MIN(exposure_us, MICROSECOND_CLKS / 2) * (MT9V034_XCLK_FREQ / MICROSECOND_CLKS);
        int row_time = row_time_0 + row_time_1;
        int coarse_time = exposure / row_time;
        int fine_time = exposure % row_time;

        ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_TOTAL_SHUTTER_WIDTH, coarse_time);
        ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_TOTAL_SHUTTER_WIDTH_B, coarse_time);
        ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_FINE_SHUTTER_WIDTH_TOTAL, fine_time);
        ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_FINE_SHUTTER_WIDTH_TOTAL_B, fine_time);
    } else if ((enable != 0) && (exposure_us >= 0)) {
        ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_WINDOW_WIDTH, &row_time_0);
        ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_HORIZONTAL_BLANKING, &row_time_1);

        int exposure = IM_MIN(exposure_us, MICROSECOND_CLKS / 2) * (MT9V034_XCLK_FREQ / MICROSECOND_CLKS);
        int row_time = row_time_0 + row_time_1;
        int coarse_time = exposure / row_time;

        ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_MAX_EXPOSE, coarse_time);
    }

    return ret;
}

static int get_exposure_us(sensor_t *sensor, int *exposure_us)
{
    uint16_t int_rows, int_pixels, row_time_0, row_time_1;
    int ret = cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_TOTAL_SHUTTER_WIDTH, &int_rows);
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_FINE_SHUTTER_WIDTH_TOTAL, &int_pixels);
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_WINDOW_WIDTH, &row_time_0);
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_HORIZONTAL_BLANKING, &row_time_1);
    *exposure_us = ((int_rows * (row_time_0 + row_time_1)) + int_pixels) / (MT9V034_XCLK_FREQ / MICROSECOND_CLKS);
    return ret;
}

static int set_hmirror(sensor_t *sensor, int enable)
{
    uint16_t read_mode;
    int ret = cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE, &read_mode);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE, // inverted behavior
            (read_mode & (~MT9V034_READ_MODE_COL_FLIP)) | ((enable == 0) ? MT9V034_READ_MODE_COL_FLIP : 0));
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE_B, &read_mode);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE_B, // inverted behavior
            (read_mode & (~MT9V034_READ_MODE_COL_FLIP)) | ((enable == 0) ? MT9V034_READ_MODE_COL_FLIP : 0));
    ret |= sensor->snapshot(sensor, NULL, NULL); // Force shadow mode register to update...
    return ret;
}

static int set_vflip(sensor_t *sensor, int enable)
{
    uint16_t read_mode;
    int ret = cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE, &read_mode);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE, // inverted behavior
            (read_mode & (~MT9V034_READ_MODE_ROW_FLIP)) | ((enable == 0) ? MT9V034_READ_MODE_ROW_FLIP : 0));
    ret |= cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE_B, &read_mode);
    ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_READ_MODE_B, // inverted behavior
            (read_mode & (~MT9V034_READ_MODE_ROW_FLIP)) | ((enable == 0) ? MT9V034_READ_MODE_ROW_FLIP : 0));
    ret |= sensor->snapshot(sensor, NULL, NULL); // Force shadow mode register to update...
    return ret;
}

static int ioctl(sensor_t *sensor, int request, va_list ap)
{
    int ret = 0;
    uint16_t chip_control;

    // The MT9V034 does not have a hardware scaler so the readout w/h must be equal to the
    // framesize w/h.
    int tmp_readout_w = resolution[sensor->framesize][0];
    int tmp_readout_h = resolution[sensor->framesize][1];
    if (sensor->framesize == FRAMESIZE_INVALID) {
        tmp_readout_w = ACTIVE_SENSOR_WIDTH;
        tmp_readout_h = ACTIVE_SENSOR_HEIGHT;
    }

    if ((tmp_readout_w <= (ACTIVE_SENSOR_WIDTH / 4)) && (tmp_readout_h <= (ACTIVE_SENSOR_HEIGHT / 4))) {
        tmp_readout_w *= 4;
        tmp_readout_h *= 4;
    } else if ((tmp_readout_w <= (ACTIVE_SENSOR_WIDTH / 2)) && (tmp_readout_h <= (ACTIVE_SENSOR_HEIGHT / 2))) {
        tmp_readout_w *= 2;
        tmp_readout_h *= 2;
    }

    switch (request) {
        case IOCTL_SET_READOUT_WINDOW: {
            int tmp_readout_x = va_arg(ap, int);
            int tmp_readout_y = va_arg(ap, int);
            int readout_x_max = (ACTIVE_SENSOR_WIDTH - tmp_readout_w) / 2;
            int readout_y_max = (ACTIVE_SENSOR_HEIGHT - tmp_readout_h) / 2;
            tmp_readout_x = IM_MAX(IM_MIN(tmp_readout_x, readout_x_max), -readout_x_max);
            tmp_readout_y = IM_MAX(IM_MIN(tmp_readout_y, readout_y_max), -readout_y_max);
            bool changed = (tmp_readout_x != readout_x) ||
                           (tmp_readout_y != readout_y);
            readout_x = tmp_readout_x;
            readout_y = tmp_readout_y;
            if (changed && (sensor->framesize != FRAMESIZE_INVALID)) {
                ret |= set_framesize(sensor, sensor->framesize);
            }
            break;
        }
        case IOCTL_GET_READOUT_WINDOW: {
            *va_arg(ap, int *) = readout_x;
            *va_arg(ap, int *) = readout_y;
            *va_arg(ap, int *) = tmp_readout_w;
            *va_arg(ap, int *) = tmp_readout_h;
            break;
        }
        case IOCTL_SET_TRIGGERED_MODE: {
            int enable = va_arg(ap, int);
            ret  = cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_CHIP_CONTROL, &chip_control);
            ret |= cambus_writew(&sensor->bus, sensor->slv_addr, MT9V034_CHIP_CONTROL,
                    (chip_control & (~MT9V034_CHIP_CONTROL_MODE_MASK))
                    | ((enable != 0) ? MT9V034_CHIP_CONTROL_SNAP_MODE : MT9V034_CHIP_CONTROL_MASTER_MODE));
            ret |= sensor->snapshot(sensor, NULL, NULL); // Force shadow mode register to update...
            break;
        }
        case IOCTL_GET_TRIGGERED_MODE: {
            int *enable = va_arg(ap, int *);
            ret = cambus_readw(&sensor->bus, sensor->slv_addr, MT9V034_CHIP_CONTROL, &chip_control);
            if (ret >= 0) {
                *enable = ((chip_control & MT9V034_CHIP_CONTROL_MODE_MASK) == MT9V034_CHIP_CONTROL_SNAP_MODE);
            }
            break;
        }
        default: {
            ret = -1;
            break;
        }
    }

    return ret;
}

int mt9v034_init(sensor_t *sensor)
{
    // Initialize sensor structure.
    sensor->gs_bpp              = sizeof(uint8_t);
    sensor->reset               = reset;
    sensor->read_reg            = read_reg;
    sensor->write_reg           = write_reg;
    sensor->set_pixformat       = set_pixformat;
    sensor->set_framesize       = set_framesize;
    sensor->set_colorbar        = set_colorbar;
    sensor->set_auto_gain       = set_auto_gain;
    sensor->get_gain_db         = get_gain_db;
    sensor->set_auto_exposure   = set_auto_exposure;
    sensor->get_exposure_us     = get_exposure_us;
    sensor->set_hmirror         = set_hmirror;
    sensor->set_vflip           = set_vflip;
    sensor->ioctl               = ioctl;

    // Set sensor flags
    SENSOR_HW_FLAGS_SET(sensor, SENSOR_HW_FLAGS_VSYNC, 0);
    SENSOR_HW_FLAGS_SET(sensor, SENSOR_HW_FLAGS_HSYNC, 0);
    SENSOR_HW_FLAGS_SET(sensor, SENSOR_HW_FLAGS_PIXCK, 0);
    SENSOR_HW_FLAGS_SET(sensor, SENSOR_HW_FLAGS_FSYNC, 1);
    SENSOR_HW_FLAGS_SET(sensor, SENSOR_HW_FLAGS_JPEGE, 0);
    SENSOR_HW_FLAGS_SET(sensor, SWNSOR_HW_FLAGS_RGB565_REV, 0);

    return 0;
}

#endif // (OMV_ENABLE_MT9V034 == 1)
