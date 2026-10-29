/* wuss/test/all-tests.h -- the Wuss test suite's entry points */

#ifndef WUSS_TEST_ALL_TESTS_H
#define WUSS_TEST_ALL_TESTS_H

#include "base/result.h"

typedef result_t (testfn_t)(const char *resources);

extern testfn_t gridview_bench_test,
                paintingdoc_test,
                wuss_menu_test,
                wuss_pointer_test,
                wuss_pointer_bounds_test,
                wuss_message_test,
                wuss_drag_test,
                wuss_task_test,
                wuss_content_test,
                wuss_stack_test,
                wuss_toolbar_test,
                wuss_pane_test,
                wuss_lifecycle_test,
                wuss_invalidate_test,
                wuss_placement_test,
                wuss_focus_test,
                wuss_hidden_test,
                wuss_move_test,
                wuss_resize_test,
                wuss_scroll_test,
                wuss_toggle_test,
                wuss_furniture_test,
                wuss_iconpool_test,
                wuss_icon_test,
                wuss_icon_draw_test,
                wuss_writable_test,
                wuss_slider_test,
                wuss_hover_test,
                wuss_click_test,
                wuss_set_text_test,
                wuss_iconbar_test,
                wuss_menu_desc_test,
                wuss_gadget_test,
                wuss_component_test,
                dcs_test,
                error_test,
                colourpicker_test,
                wuss_test;

#endif /* WUSS_TEST_ALL_TESTS_H */
