// vim: set fileencoding=utf-8
#include <stdlib.h>
#include "naomi/video.h"

void test_truetype_metrics(test_context_t *context)
{
#if __has_include(<ft2build.h>)
    extern uint8_t *dejavusans_ttf_data;
    extern unsigned int dejavusans_ttf_len;
    font_t *font_12pt = video_font_add(dejavusans_ttf_data, dejavusans_ttf_len);
    video_font_set_size(font_12pt, 12);

    font_metrics_t metrics = video_get_text_metrics(font_12pt, "Hello!");
    ASSERT(metrics.width == 34, "Invalid width %d returned from metrics!", metrics.width);
    ASSERT(metrics.height == 12, "Invalid height %d returned from metrics!", metrics.height);

    metrics = video_get_text_metrics(font_12pt, "γεια σας!");
    ASSERT(metrics.width == 57, "Invalid width %d returned from metrics!", metrics.width);
    ASSERT(metrics.height == 12, "Invalid height %d returned from metrics!", metrics.height);

    metrics = video_get_text_metrics(font_12pt, "Hello!\n123");
    ASSERT(metrics.width == 34, "Invalid width %d returned from metrics!", metrics.width);
    ASSERT(metrics.height == 24, "Invalid height %d returned from metrics!", metrics.height);

    metrics = video_get_text_metrics(font_12pt, "123\nHello!\n");
    ASSERT(metrics.width == 34, "Invalid width %d returned from metrics!", metrics.width);
    ASSERT(metrics.height == 24, "Invalid height %d returned from metrics!", metrics.height);

    metrics = video_get_character_metrics(font_12pt, 'H');
    ASSERT(metrics.width == 9, "Invalid width %d returned from metrics!", metrics.width);
    ASSERT(metrics.height == 12, "Invalid height %d returned from metrics!", metrics.height);

    metrics = video_get_character_metrics(font_12pt, '!');
    ASSERT(metrics.width == 5, "Invalid width %d returned from metrics!", metrics.width);
    ASSERT(metrics.height == 12, "Invalid height %d returned from metrics!", metrics.height);
#else
    SKIP("freetype is not installed");
#endif
}
