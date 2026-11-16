#include "sysinfo.h"

#include <time.h>

#include <switch.h>

#define REFRESH_NS 2000000000ull
#define FIRST_VALID_YEAR 2020

static bool g_has_psm;
static SysInfo g_cached;
static uint64_t g_next_refresh_ns;

void sysinfoInit(void)
{
    g_has_psm = R_SUCCEEDED(psmInitialize());
}

void sysinfoExit(void)
{
    if (g_has_psm)
        psmExit();
    g_has_psm = false;
}

static void readClock(SysInfo *info)
{
    time_t now = time(NULL);
    struct tm local;
    info->has_time = localtime_r(&now, &local) && local.tm_year + 1900 >= FIRST_VALID_YEAR;
    if (info->has_time) {
        info->hour = local.tm_hour;
        info->minute = local.tm_min;
    }
}

static void readBattery(SysInfo *info)
{
    u32 percent = 0;
    PsmChargerType charger = PsmChargerType_Unconnected;
    info->has_battery = g_has_psm && R_SUCCEEDED(psmGetBatteryChargePercentage(&percent)) && percent <= 100;
    if (!info->has_battery)
        return;
    info->battery_percent = (int)percent;
    // Ask several ways: not every call answers the same on Switch 2.
    bool supplied = false;
    PsmBatteryChargeInfoFields fields;
    info->charging = (R_SUCCEEDED(psmGetChargerType(&charger)) && charger != PsmChargerType_Unconnected) ||
                     (R_SUCCEEDED(psmIsEnoughPowerSupplied(&supplied)) && supplied) ||
                     (R_SUCCEEDED(psmGetBatteryChargeInfoFields(&fields)) &&
                      (fields.battery_charging || fields.charger_type != PsmChargerType_Unconnected));
}

void sysinfoGet(SysInfo *info, uint64_t now_ns)
{
    if (now_ns >= g_next_refresh_ns) {
        readClock(&g_cached);
        readBattery(&g_cached);
        g_next_refresh_ns = now_ns + REFRESH_NS;
    }
    *info = g_cached;
}
