#include "wifi_power.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    vibe_wifi_power_t state={0};
    assert(vibe_wifi_power_next(&state,false,false,false,false)==V_WIFI_POWER_UNCHANGED);
    assert(vibe_wifi_power_next(&state,true,false,false,false)==V_WIFI_POWER_MODEM);
    for(unsigned i=0;i<100;i++)
        assert(vibe_wifi_power_next(&state,true,false,false,false)==V_WIFI_POWER_UNCHANGED);
    assert(vibe_wifi_power_next(&state,true,false,true,false)==V_WIFI_POWER_AWAKE);
    assert(vibe_wifi_power_next(&state,true,false,true,true)==V_WIFI_POWER_UNCHANGED);
    assert(vibe_wifi_power_next(&state,true,false,false,true)==V_WIFI_POWER_UNCHANGED);
    assert(vibe_wifi_power_next(&state,true,false,false,false)==V_WIFI_POWER_MODEM);
    assert(vibe_wifi_power_next(&state,true,false,false,true)==V_WIFI_POWER_AWAKE);
    assert(vibe_wifi_power_next(&state,true,false,true,true)==V_WIFI_POWER_UNCHANGED);
    assert(vibe_wifi_power_next(&state,true,false,true,false)==V_WIFI_POWER_UNCHANGED);
    assert(vibe_wifi_power_next(&state,true,false,false,false)==V_WIFI_POWER_MODEM);
    /* Mode changes and driver restarts reapply the same requested policy. */
    assert(vibe_wifi_power_next(&state,true,true,false,false)==V_WIFI_POWER_MODEM);
    assert(vibe_wifi_power_next(&state,true,false,false,false)==V_WIFI_POWER_MODEM);
    assert(vibe_wifi_power_next(&state,false,false,true,false)==V_WIFI_POWER_UNCHANGED);
    assert(vibe_wifi_power_next(&state,true,false,true,false)==V_WIFI_POWER_AWAKE);
    for(unsigned cycle=0;cycle<100;cycle++) {
        assert(vibe_wifi_power_next(&state,true,false,true,false)==V_WIFI_POWER_UNCHANGED);
        assert(vibe_wifi_power_next(&state,true,false,false,false)==V_WIFI_POWER_MODEM);
        assert(vibe_wifi_power_next(&state,true,false,true,false)==V_WIFI_POWER_AWAKE);
    }
    puts("Wi-Fi power: overlapping voice/radio lifetime, idle coalescing, AP mode and driver restart PASS");
}
