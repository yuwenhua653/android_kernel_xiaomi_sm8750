// SPDX-License-Identifier: GPL-2.0
/*
 * mca_charge_mievent->c
 *
 * mca charger mievent driver
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_log.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/timekeeping.h>
#include <mca/common/mca_event.h>

#if IS_ENABLED(CONFIG_MIEV)
#include <miev/mievent.h>
#endif

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_charge_mievent"
#endif

#if IS_ENABLED(CONFIG_MIEV)
static struct charge_mievent_info g_charge_mievent_info[] = {
	/* charger mievent upload type for plug*/
	{
		.event_code = MIEVENT_CODE_PD_AUTH_FAILED,
		.event_type = "chgErrInfo",
		.event_describe = "PdAuthFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_PD_AUTH_FAILED,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "adapterId" },
	},
	{
		.event_code = MIEVENT_CODE_CP_OPEN_FAILED,
		.event_type = "chgErrInfo",
		.event_describe = "CpEnFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_OPEN_FAILED,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "cpId" },
	},
	{
		.event_code = MIEVENT_CODE_NON_STANDARD_ADAPTER,
		.event_type = "chgErrInfo",
		.event_describe = "NoneStandartChg",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_NOT_STANDARD_ADAPTER,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_RP_SHORT_VBUS_DETECTED,
		.event_type = "chgErrInfo",
		.event_describe = "CorrosionDischarge",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_RP_SHORT_VBUS_DETECTED,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{ .event_code = MIEVENT_CODE_LPD_DETECTED,
	  .event_type = "chgErrInfo",
	  .event_describe = "LpdDetected",
	  .upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
	  .type_index = PLUG_TYPE_LPD_DETECTED,
	  .data_type = MIEVENT_DATA_TYPE_INT,
	  .data_count = 1,
	  .para_name = { "lpdFlag" } },
	{
		.event_code = MIEVENT_CODE_CP_VBUS_OVP,
		.event_type = "chgErrInfo",
		.event_describe = "CpVbusOvp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_VBUS_OVP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_CP_IBUS_OCP,
		.event_type = "chgErrInfo",
		.event_describe = "CpIbusOcp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_IBUS_OCP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_CP_VBAT_OVP,
		.event_type = "chgErrInfo",
		.event_describe = "CpVbatOvp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_VBAT_OVP,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = { "vbat", "vbatMax" },
	},
	{
		.event_code = MIEVENT_CODE_CP_IBAT_OCP,
		.event_type = "chgErrInfo",
		.event_describe = "CpIbatOcp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_IBAT_OCP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_CP_VAC_OVP,
		.event_type = "chgErrInfo",
		.event_describe = "CpVacOvp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_VAC_OVP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_ANTI_BURN_TRIGGERED,
		.event_type = "chgStatInfo",
		.event_describe = "AntiBurnTirg",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_ANTI_BURN_TRIGGERED,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "tconn" },
	},
	{
		.event_code = MIEVENT_CODE_SOC_NOT_FULL,
		.event_type = "chgErrInfo",
		.event_describe = "SocNotFull",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_SOC_NOT_FULL,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 3,
		.para_name = { "vbat", "soc", "rsoc" },
	},
	{ .event_code = MIEVENT_CODE_SMART_ENDURANCE_TRIGGERED,
	  .event_type = "chgStatInfo",
	  .event_describe = "SmartEnduraTrig",
	  .upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
	  .type_index = PLUG_TYPE_SMART_ENDURANCE_TRIGGERED,
	  .data_type = MIEVENT_DATA_TYPE_INT,
	  .data_count = 1,
	  .para_name = { "soc" } },
	{
		.event_code = MIEVENT_CODE_SMART_NAVIGATION_TRIGGERED,
		.event_type = "chgStatInfo",
		.event_describe = "SmartNaviTrig",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_SMART_NAVIGATION_TRIGGERED,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "soc" },
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_MISSING,
		.event_type = "chgErrInfo",
		.event_describe = "BattLinkerAbsent",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_MISSING,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_CP_TDIE_HOT,
		.event_type = "chgStatInfo",
		.event_describe = "CpTdieHot",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_TDIE_HOT,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = { "masterTdie", "slaveTdie" },
	},
	{
		.event_code = MIEVENT_CODE_VBUS_UVLO,
		.event_type = "chgErrInfo",
		.event_describe = "VbusUvlo",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_VBUS_UVLO,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 3,
		.para_name = { "vbat", "vbus", "aicl" },
	},
	{
		.event_code = MIEVENT_CODE_LOW_TEMP_DISCHARGING,
		.event_type = "chgErrInfo",
		.event_describe = "NotChgInLowTemp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_LOW_TEMP_DISCHARGING,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "tbat" },
	},
	{
		.event_code = MIEVENT_CODE_HIGH_TEMP_DISCHARGING,
		.event_type = "chgErrInfo",
		.event_describe = "NotChgInHighTemp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_HIGH_TEMP_DISCHARGING,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "tbat" },
	},
	{
		.event_code = MIEVENT_CODE_DUAL_BATTERY_MISSING,
		.event_type = "chgErrInfo",
		.event_describe = "DualBattLinkerAbsent",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_DUAL_BATTERY_MISSING,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "status" },
	},
	{
		.event_code = MIEVENT_CODE_SMART_ENDURANCE_SOC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "SmartEnduraSocErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_SMART_ENDURANCE_SOC_ERR,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "soc" },
	},
	{
		.event_code = MIEVENT_CODE_SMART_NAVIGATION_SOC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "SmartNaviSocErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_SMART_NAVIGATION_SOC_ERR,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "soc" },
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_AUTH_FAIL,
		.event_type = "chgErrInfo",
		.event_describe = "BattAuthFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_AUTH_FAIL,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_DUAL_BATTERY_AUTH_FAIL,
		.event_type = "chgErrInfo",
		.event_describe = "ChgBattAuthFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_DUAL_BATTERY_AUTH_FAIL,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "status" },
	},
	{
		.event_code = MIEVENT_CODE_ANTIBURN_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "AntiFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_ANTIBURN_ERR,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_FASTCHG_FAIL,
		.event_type = "chgErrInfo",
		.event_describe = "WlsFastChgFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_FASTCHG_FAIL,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_FOD_LOW_POWER,
		.event_type = "chgErrInfo",
		.event_describe = "WlsQLow",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_FOD_LOW_POWER,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = { "chgQbase", "chgQreal" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_RX_OTP,
		.event_type = "chgErrInfo",
		.event_describe = "WlsRxOTP",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_RX_OTP,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "chgRxtemp" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_RX_OVP,
		.event_type = "chgErrInfo",
		.event_describe = "WlsRxOVP",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_RX_OVP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_RX_OCP,
		.event_type = "chgErrInfo",
		.event_describe = "WlsRxOCP",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_RX_OCP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_TRX_FOD,
		.event_type = "chgErrInfo",
		.event_describe = "WlsTrxFod",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_TRX_FOD,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_TRX_OCP,
		.event_type = "chgErrInfo",
		.event_describe = "WlsTrxOCP",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_TRX_OCP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_TRX_UVLO,
		.event_type = "chgErrInfo",
		.event_describe = "WlsTrxUVLO",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_TRX_UVLO,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_TRX_IIC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "WlsTrxI2cErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_TRX_IIC_ERR,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_RX_IIC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "WlsRxI2cErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_RX_IIC_ERR,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_LOAD_SWITCH_I2C_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "LoadSwitchI2cErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_LOAD_SWITCH_I2C_ERR,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_FW_UPGRADE_FAIL,
		.event_type = "chgErrInfo",
		.event_describe = "WlsFwUpgradeErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_FW_UPGRADE_FAIL,
		.data_type = MIEVENT_DATA_TYPE_STRING,
		.data_count = 1,
		.para_name = { "errReason" },
	},
	/* charger mievent upload type for time*/
	{
		.event_code = MIEVENT_CODE_BATTERY_CYCLECOUNT,
		.event_type = "chgStatInfo",
		.event_describe = "chgBattCycle",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_BATTERY_CYCLECOUNT,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "cycleCnt" },
	},
	{
		.event_code = MIEVENT_CODE_FG_IIC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "FgI2cErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_FG_IIC_ERR,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "soc" },
	},
	{
		.event_code = MIEVENT_CODE_CP_ABSENT,
		.event_type = "chgErrInfo",
		.event_describe = "CpI2cErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_CP_ABSENT,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = { "masterOk", "slaveOk" },
	},
	{
		.event_code = MIEVENT_CODE_VBATT_SOC_NOT_MATCH,
		.event_type = "chgErrInfo",
		.event_describe = "VbatSocNotMatch",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_VBATT_SOC_NOT_MATCH,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 3,
		.para_name = { "vbat", "soc", "cycleCnt" },
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_TEMP_HOT,
		.event_type = "chgStatInfo",
		.event_describe = "TbatHot",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_BATTERY_TEMP_HOT,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 4,
		.para_name = { "tbat", "tbatMax", "isCharging", "tboard" },
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_TEMP_COLD,
		.event_type = "chgStatInfo",
		.event_describe = "TbatCold",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_BATTERY_TEMP_COLD,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 4,
		.para_name = { "tbat", "tbatMin", "isCharging", "tboard" },
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_VOLTAGE_DIFFER,
		.event_type = "chgStatInfo",
		.event_describe = "DualVbatDiff",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_BATTERY_VOLTAGE_DIFFER,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = { "chgBaseBattVol", "chgFlipBattVol" },
	},
	{
		.event_code = MIEVENT_CODE_WLS_MAGNETIC_CASE_ATTACH,
		.event_type = "chgStatInfo",
		.event_describe = "WlsMagCaseAttach",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_WLS_MAGNETIC_CASE_ATTACH,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = { "" },
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_OCD,
		.event_type = "chgErrInfo",
		.event_describe = "BatteryOcd",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_OCD,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 7,
		.para_name = { "ocdCount", "ocdLastCycle", "hocdCount",
			       "hocdLastCycle", "uiSoc", "voltage", "temp" },
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_CUV,
		.event_type = "chgErrInfo",
		.event_describe = "BatteryCuv",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_CUV,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 7,
		.para_name = { "cuvCount", "cuvLastCycle", "hcuvCount",
			       "hcuvLastCycle", "uiSoc", "voltage", "temp" },
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_HSCD,
		.event_type = "chgErrInfo",
		.event_describe = "BatteryScd",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_HSCD,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = { "hscdCount", "hscdLastCycle" },
	},
	{
		.event_code = MIEVENT_CODE_CHARGE_SLOWLY,
		.event_type = "chgStatInfo",
		.event_describe = "ChargeSlowly",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CHARGE_SLOWLY,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 5,
		.para_name = { "maxPower", "initialRm", "endRm", "initialSoc",
			       "endSoc" },
	},
	{
		.event_code = MIEVENT_CODE_NON_STANDARD_CHARGER,
		.event_type = "chgStatInfo",
		.event_describe = "NonStandardCharger",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_NON_STANDARD_CHARGER,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = { "chgType" },
	},
};

static struct mievent_upload_type_plug
	g_upload_type_plug_info[PLUG_TYPE_MAX_NUM] = {
		{ DEFAULT_MAX_COUNT, 0 },	{ LEVEL0_MAX_REPORT_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ LEVEL0_MAX_REPORT_COUNT, 0 },
		{ LEVEL0_MAX_REPORT_COUNT, 0 }, { LEVEL0_MAX_REPORT_COUNT, 0 },
		{ LEVEL0_MAX_REPORT_COUNT, 0 }, { LEVEL0_MAX_REPORT_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ LEVEL0_MAX_REPORT_COUNT, 0 },
		{ LEVEL0_MAX_REPORT_COUNT, 0 }, { DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ LEVEL0_MAX_REPORT_COUNT, 0 }, { DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },	{ DEFAULT_MAX_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },
		{ CHARGE_SLOWLY_MAX_REPORT_COUNT, 0 },
		{ DEFAULT_MAX_COUNT, 0 },
	};
static struct mievent_upload_type_time
	g_upload_type_time_info[TIME_TYPE_MAX_NUM] = {
		{ 0, MINOR_TIMER_S, 0 },    { 0, CRITICAL_TIMER_S, 0 },
		{ 0, CRITICAL_TIMER_S, 0 }, { 0, BLOCK_TIMER_S, 0 },
		{ 0, BLOCK_TIMER_S, 0 },    { 0, BLOCK_TIMER_S, 0 },
		{ 0, BLOCK_TIMER_S, 0 },
	};

static DEFINE_SPINLOCK(mievent_state_lock);

/* Claim one rate-limited report atomically across all charger consumers. */
static bool mca_mievent_claim(const struct charge_mievent_info *info)
{
	unsigned long flags;
	bool report = false;
	int index = info->type_index;
	time64_t now = ktime_get_boottime_seconds();

	spin_lock_irqsave(&mievent_state_lock, flags);
	if (info->upload_type == MIEVENT_UPLOAD_TYPE_PLUG &&
	    index >= 0 && index < ARRAY_SIZE(g_upload_type_plug_info)) {
		struct mievent_upload_type_plug *state = &g_upload_type_plug_info[index];

		if (state->count < state->max_count) {
			state->count++;
			report = true;
		}
	} else if (info->upload_type == MIEVENT_UPLOAD_TYPE_TIME &&
		   index >= 0 && index < ARRAY_SIZE(g_upload_type_time_info)) {
		struct mievent_upload_type_time *state = &g_upload_type_time_info[index];

		if (!state->count || now - state->time_last >= state->time_interval) {
			state->time_last = now;
			state->count = 1;
			report = true;
		}
	}
	spin_unlock_irqrestore(&mievent_state_lock, flags);
	return report;
}

void mca_charge_mievent_report(int event_index, void *data, int size)
{
	const struct charge_mievent_info *info;
	struct misight_mievent *event;
	struct mca_event_notify_data notify = { .event = "MCA_LOG_FULL_EVENT", .event_len = 18 };
	int ret, i;

#ifdef CONFIG_FACTORY_BUILD
	return;
#endif
	static_assert(ARRAY_SIZE(g_charge_mievent_info) == CHARGE_DFX_MAX_NUM);
	if (event_index < 0 || event_index >= ARRAY_SIZE(g_charge_mievent_info))
		return;
	info = &g_charge_mievent_info[event_index];
	if (size < 0 || size != info->data_count || size > CHARGE_MIEVENT_MAX_PARA_NUM ||
	    (size && !data))
		return;
	if (info->data_type == MIEVENT_DATA_TYPE_STRING) {
		char **strings = data;

		for (i = 0; i < size; i++)
			if (!strings[i] || strnlen(strings[i], MIEVENT_STRING_MAX_LEN) >= MIEVENT_STRING_MAX_LEN)
				return;
	}
	if (!strcmp(info->event_type, "chgErrInfo") &&
	    info->event_code != MIEVENT_CODE_NON_STANDARD_ADAPTER)
		mca_log_jirabot("%s %s\n", info->event_type, info->event_describe);
	event = cdev_tevent_alloc(info->event_code);
	if (!event)
		return;
	ret = cdev_tevent_add_str(event, info->event_type, info->event_describe);
	for (i = 0; !ret && i < size; i++) {
		switch (info->data_type) {
		case MIEVENT_DATA_TYPE_INT:
			ret = cdev_tevent_add_int(event, info->para_name[i], ((int *)data)[i]);
			break;
		case MIEVENT_DATA_TYPE_STRING:
			ret = cdev_tevent_add_str(event, info->para_name[i], ((char **)data)[i]);
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}
	if (!ret && mca_mievent_claim(info)) {
		mca_event_report_uevent(&notify);
		ret = cdev_tevent_write(event);
	}
	cdev_tevent_destroy(event);
	if (ret)
		mca_log_err("event %d could not be queued: %d\n", info->event_code, ret);
}
EXPORT_SYMBOL_GPL(mca_charge_mievent_report);

void mca_charge_mievent_set_state(enum charge_mievent_state_ele state, int value)
{
	const struct charge_mievent_info *info;
	unsigned long flags;
	int i;

#ifdef CONFIG_FACTORY_BUILD
	return;
#endif
	spin_lock_irqsave(&mievent_state_lock, flags);
	if (state == MIEVENT_STATE_PLUG && !value) {
		for (i = 0; i < ARRAY_SIZE(g_upload_type_plug_info); i++)
			g_upload_type_plug_info[i].count = 0;
	} else if (state == MIEVENT_STATE_END && value >= 0 &&
		   value < ARRAY_SIZE(g_charge_mievent_info)) {
		info = &g_charge_mievent_info[value];
		i = info->type_index;
		if (info->upload_type == MIEVENT_UPLOAD_TYPE_PLUG &&
		    i >= 0 && i < ARRAY_SIZE(g_upload_type_plug_info)) {
			g_upload_type_plug_info[i].count = 0;
		} else if (info->upload_type == MIEVENT_UPLOAD_TYPE_TIME &&
			   i >= 0 && i < ARRAY_SIZE(g_upload_type_time_info)) {
			g_upload_type_time_info[i].time_last = 0;
			g_upload_type_time_info[i].count = 0;
		}
	}
	spin_unlock_irqrestore(&mievent_state_lock, flags);
}
EXPORT_SYMBOL_GPL(mca_charge_mievent_set_state);

#else
void mca_charge_mievent_report(int event_index, void *data, int size)
{
	return;
}
EXPORT_SYMBOL_GPL(mca_charge_mievent_report);
void mca_charge_mievent_set_state(enum charge_mievent_state_ele state,
				  int value)
{
	return;
}
EXPORT_SYMBOL_GPL(mca_charge_mievent_set_state);
#endif

MODULE_DESCRIPTION("mca charge mievent");
MODULE_AUTHOR("lvxiaofeng@xiaomi.com");
MODULE_LICENSE("GPL v2");
