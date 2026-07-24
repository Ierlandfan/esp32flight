/* Settings-change signal: the settings screen bumps a generation counter
 * on save, and the flight task re-applies location/radius live on the next
 * poll - no process restart, unlike the ESP32. */

static volatile unsigned s_generation;

void apk_settings_touch(void)
{
    s_generation++;
}

unsigned apk_settings_gen(void)
{
    return s_generation;
}
