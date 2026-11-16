#include "screen.h"

#include <switch.h>

#define FADE_NS 300000000ull

static bool g_has_lbl;
static bool g_on = true;

void screenInit(void)
{
    g_has_lbl = R_SUCCEEDED(lblInitialize());
}

void screenExit(void)
{
    screenSetOn(true);
    if (g_has_lbl)
        lblExit();
    g_has_lbl = false;
}

void screenSetOn(bool on)
{
    if (on == g_on)
        return;
    g_on = on;
    if (!g_has_lbl)
        return;
    if (on)
        lblSwitchBacklightOn(FADE_NS);
    else
        lblSwitchBacklightOff(FADE_NS);
}

bool screenIsOn(void)
{
    return g_on;
}

bool screenHasBacklightControl(void)
{
    return g_has_lbl;
}
