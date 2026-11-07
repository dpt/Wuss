/* wuss/component/scaleview.h -- Scale view dialogue */

/**
 * \file scaleview.h
 *
 * A wuss component: the standard "Scale view" dialogue, as AppEngine's
 * ScaleView. It shows a percentage in a writable field with down and up
 * adjusters, buttons for 25%, 50%, 100% and 200%, buttons to fit the view to
 * the screen or to its window, and Cancel and Scale. Built on top of
 * wuss_dialogue, as with wuss_dcs.
 *
 * The preset buttons and the adjusters change the field only. SELECT on an
 * adjuster steps by wuss_SCALEVIEW_STEP towards larger scales, ADJUST
 * towards smaller ones, as in RISC OS. Scale (or RETURN) reports the field's
 * percentage, clamped to [wuss_SCALEVIEW_MIN, wuss_SCALEVIEW_MAX]; the Fit
 * buttons report a fit and leave the caller to work out the percentage.
 *
 * The dialogue runs on a task of its own, so callers forward nothing.
 * wuss_scaleview_open shows it as a menu of its own, centred on the pointer,
 * and returns at once; the caller carries on from its
 * wuss_scaleview_choice_fn_t. Cancel, ESCAPE and the dialogue being closed
 * some other way report nothing.
 *
 * Alternatively hang wuss_scaleview_window as a wuss_menu_item_t::window
 * leaf flagged wuss_MENU_ITEM_PRE_OPEN, so that it opens as a submenu. The
 * dialogue answers the leaf's wuss_EVENT_PRE_SHOW itself and closes the
 * whole chain once the user chooses. Call wuss_scaleview_set_percent before
 * opening the menu so the field shows the view's scale.
 *
 * Built only when WUSS_COMPONENTS is defined.
 */

#ifndef WUSS_COMPONENT_SCALEVIEW_H
#define WUSS_COMPONENT_SCALEVIEW_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "base/result.h"

#include "wuss/wuss.h"

/* ----------------------------------------------------------------------- */

/** Least percentage the dialogue reports. */
#define wuss_SCALEVIEW_MIN  1

/** Greatest percentage the dialogue reports. */
#define wuss_SCALEVIEW_MAX  999

/** Percentage an adjuster click steps by. */
#define wuss_SCALEVIEW_STEP 10

/** Opaque handle: owns the dialogue, its icons and its task. */
typedef struct wuss_scaleview wuss_scaleview_t;

/** The user's choice. */
typedef enum
{
  wuss_SCALEVIEW_SCALE,      /**< Scale to the reported percentage. */
  wuss_SCALEVIEW_FIT_SCREEN, /**< Scale so the whole view fits the screen. */
  wuss_SCALEVIEW_FIT_WINDOW  /**< Scale so the whole view fits its window. */
}
wuss_scaleview_choice_t;

/**
 * Choice callback: the user has chosen. Called after the dialogue has been
 * dismissed. The dialogue may be destroyed from here, or reopened.
 *
 * \param[in] choice  What the user chose.
 * \param[in] percent The scale for wuss_SCALEVIEW_SCALE; 0 otherwise.
 * \param[in] opaque  As passed to wuss_scaleview_create.
 */
typedef void (wuss_scaleview_choice_fn_t)(wuss_scaleview_choice_t choice,
                                          int                     percent,
                                          void                   *opaque);

/* ----------------------------------------------------------------------- */

/**
 * Create a Scale view dialogue: a hidden window on a task of its own.
 *
 * \param[out] out    Filled with the new handle on success, untouched on
 *                    failure.
 * \param[in]  wuss   Window manager.
 * \param[in]  fn     Called with the user's choice.
 * \param[in]  opaque Passed back to \p fn.
 * \return \ref result_OK, \ref result_OOM, \ref result_NULL_ARG if \p out,
 *         \p wuss or \p fn is NULL, or a wuss_task_create /
 *         wuss_window_create / wuss_icon_create code.
 */
result_t wuss_scaleview_create(wuss_scaleview_t          **out,
                               wuss_t                     *wuss,
                               wuss_scaleview_choice_fn_t *fn,
                               void                       *opaque);

/**
 * Free a dialogue: dismisses it without reporting a choice, then closes its
 * window and task. Safe to pass NULL.
 *
 * \param[in] doomed Handle to free, or NULL.
 */
void wuss_scaleview_destroy(wuss_scaleview_t *doomed);

/**
 * Show the dialogue, centred on the pointer, as a menu of its own, with \p
 * percent in the field. Any menu chain already open is closed first.
 *
 * \param[in] sv      Handle.
 * \param[in] percent The view's current scale.
 * \return \ref result_OK, or a wuss_icon_set_text / wuss_menu_open_window
 *         code, in which case the dialogue is not shown.
 */
result_t wuss_scaleview_open(wuss_scaleview_t *sv, int percent);

/**
 * Put \p percent, clamped, in the field.
 *
 * \param[in] sv      Handle.
 * \param[in] percent The view's current scale.
 * \return \ref result_OK, or a wuss_icon_set_text code.
 */
result_t wuss_scaleview_set_percent(wuss_scaleview_t *sv, int percent);

/**
 * The dialogue's window, to hang as a menu leaf. See above.
 *
 * \param[in] sv Handle.
 * \return The window; owned by the dialogue.
 */
wuss_window_t *wuss_scaleview_window(const wuss_scaleview_t *sv);

#ifdef __cplusplus
}
#endif

#endif /* WUSS_COMPONENT_SCALEVIEW_H */
