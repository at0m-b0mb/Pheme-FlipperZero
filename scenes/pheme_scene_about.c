#include "../pheme_i.h"

void pheme_scene_about_on_enter(void* context) {
    PhemeApp* app = context;

    widget_reset(app->widget);

    widget_add_text_scroll_element(
        app->widget,
        0,
        0,
        128,
        64,
        "\e#Pheme " PHEME_VERSION "\e#\n"
        "POCSAG pager privacy educator\n"
        "\n"
        "Paging was standardised in 1982 and a great deal of it is still "
        "running. Hospitals use it because it works in basements and lift "
        "shafts; fire and industrial sites use it because it does not depend "
        "on anything staying up.\n"
        "\n"
        "It has no encryption and no authentication. None. A page goes out in "
        "clear to every receiver in range, and the capcode it is addressed to "
        "is a number burned into one pager for the life of that pager.\n"
        "\n"
        "\e#What it does\e#\n"
        "Decodes POCSAG at 512, 1200 and 2400 bps, repairs what the BCH code "
        "can repair, and grades every page for how much it gives away about "
        "a person. Messages are redacted on screen by default. The plain text "
        "needs a setting turned on and a long press.\n"
        "\n"
        "\e#What it will not do\e#\n"
        "Transmit. There is no transmit path in the app. It does not page "
        "anyone, jam anything, or touch the network it is listening to - "
        "which is the uncomfortable part, because neither does anybody else "
        "who is listening.\n"
        "\n"
        "\e#Honest limits\e#\n"
        "The CC1101 tunes 300-348, 387-464 and 779-928 MHz. VHF paging, UK "
        "and European commercial paging at 466 MHz and US national paging at "
        "929-932 MHz are all out of reach. FLEX is a different protocol and "
        "is not decoded. A blank screen may mean a quiet channel or a deaf "
        "radio, and the app says so rather than letting you assume.\n"
        "\n"
        "\e#Use it lawfully\e#\n"
        "Receiving is not legal everywhere, and acting on what you hear is "
        "legal almost nowhere. The point of this app is to make the case for "
        "encrypted messaging in the places that still page in clear - not to "
        "read anybody's afternoon.\n"
        "\n"
        "at0m-b0mb\n"
        "github.com/at0m-b0mb/Pheme-FlipperZero\n");

    view_dispatcher_switch_to_view(app->view_dispatcher, PhemeViewWidget);
}

bool pheme_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void pheme_scene_about_on_exit(void* context) {
    PhemeApp* app = context;
    widget_reset(app->widget);
}
