#include "test.h"

void test_theme(void)
{
    expect(theme_color_ok("#FFFFFF") == 1, "color upper");
    expect(theme_color_ok("#ffffff") == 1, "color lower");
    expect(theme_color_ok("#FFF") == 0, "color short");
    expect(theme_color_ok("FFFFFF") == 0, "color no hash");
    expect(theme_color_ok("#GGGGGG") == 0, "color invalid hex");
    expect(theme_color_ok(NULL) == 0, "color null");
    expect(binding_str_to_key("F1") == KEY_F(1), "bind F1");
    expect(binding_str_to_key("UP") == KEY_UP, "bind up");

    int c = color_to_256("#000000");
    expect(c == 16, "color black to 256");
    c = color_to_256("#FF0000");
    expect(c == 196, "color red to 256");
}
