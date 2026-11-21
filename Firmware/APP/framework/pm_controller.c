/* Power state controller: drives the policy and calls the device hooks on the
 * state transitions.
 *
 * Ported from Pocket_PowerBank (Applications/framework/pm_controller.c): the
 * only difference against the NUEDC_2025B original is the fixed
 * pm_controller_resume_idle() (see the comment there), which the latter
 * framework fixed after the first project hit "sleeps the moment the
 * charging/discharging block is released". */
#include "pm_controller.h"

#include "pm_device.h"
#include "pm_sleep_timer.h"

#ifndef PM_PENDING_HOLD_TICKS
#define PM_PENDING_HOLD_TICKS 3
#endif

static pm_hook_result_t pm_handle_transition(pm_power_state_t from,pm_power_state_t to) {
    if((from==PM_STATE_RUN && to==PM_STATE_UI_OFF) ||
        (from==PM_STATE_UI_OFF && to==PM_STATE_SLEEP_PREPARE))
    return pm_device_prepare_all();
    if(from==PM_STATE_SLEEP_PREPARE && to==PM_STATE_DEEPSLEEP)
        return pm_device_suspend_all();
    return PM_HOOK_DONE;
}

void pm_controller_init(pm_controller_t *ctx, uint32_t idle_timeout_ms) {
    if (ctx == 0) {
        return;
    }

    ctx->state = PM_STATE_RUN;
    ctx->ui_active = 1;
    ctx->usb_or_dc_attached = 0;
    ctx->low_battery = 0;
    ctx->force_standby = 0;
    ctx->wake_latched = 0;
    ctx->post_hold_ticks = 0;
    ctx->idle_timer_paused = 0;
    ctx->transition_pending = 0;
    ctx->resuming = 0;

    pm_sleep_timer_init(idle_timeout_ms);
}

void pm_controller_mark_ui_active(pm_controller_t *ctx, uint8_t active) {
    if (ctx == 0) {
        return;
    }

    ctx->ui_active = (active != 0) ? 1 : 0;
    if (ctx->ui_active != 0) {
        pm_controller_refresh_idle(ctx);
    }
}

void pm_controller_set_usb_attached(pm_controller_t *ctx, uint8_t attached) {
    if (ctx == 0) {
        return;
    }

    ctx->usb_or_dc_attached = (attached != 0) ? 1 : 0;
    if (ctx->usb_or_dc_attached != 0) {
        pm_controller_refresh_idle(ctx);
    }
}

void pm_controller_set_low_battery(pm_controller_t *ctx, uint8_t low_battery) {
    if (ctx == 0) {
        return;
    }

    ctx->low_battery = (low_battery != 0) ? 1 : 0;
}

void pm_controller_force_standby(pm_controller_t *ctx, uint8_t force_standby) {
    if (ctx == 0) {
        return;
    }

    ctx->force_standby = (force_standby != 0) ? 1 : 0;
}

void pm_controller_refresh_idle(pm_controller_t *ctx) {
    if (ctx == 0) {
        return;
    }

    /* External activity -> the UI becomes active again. */
    ctx->ui_active = 1;
    ctx->wake_latched = 1;
    /* While paused, refresh restarts the *frozen* remainder. */
    pm_sleep_timer_refresh();
}

void pm_controller_pause_idle(pm_controller_t *ctx) {
    if (ctx == 0) {
        return;
    }

    if (ctx->idle_timer_paused == 0) {
        pm_sleep_timer_pause();
        ctx->idle_timer_paused = 1;
    }
}

void pm_controller_resume_idle(pm_controller_t *ctx) {
    if (ctx != 0 && ctx->idle_timer_paused != 0) {
        /* Resume from frozen time; do not replace it with a fresh timeout. */
        pm_sleep_timer_resume();
        ctx->idle_timer_paused = 0;
    }
}

void pm_controller_notify_wake(pm_controller_t *ctx) {
    if (ctx == 0) {
        return;
    }

    ctx->wake_latched = 1;
}

void pm_controller_enter_background_check(pm_controller_t *ctx) {
    if(!ctx)return;
    /* STOP1 has physically returned; resume_all() was deliberately
     * bypassed. Complete only selective background device work. */
    ctx->state=PM_STATE_UI_OFF;
    ctx->ui_active=0u;
    ctx->wake_latched=0u;
    ctx->post_hold_ticks=0u;
    ctx->transition_pending=0u;
    ctx->resuming=0u;
}

uint8_t pm_controller_is_ui_blocked(pm_controller_t *ctx) {
    if (ctx == 0) {
        return 0;
    }

    /* Outside RUN the UI must not draw. */
    return (ctx->state != PM_STATE_RUN || ctx->transition_pending || ctx->resuming) ? 1 : 0;
}

static pm_power_state_t finish_resume(pm_controller_t *ctx) {
    pm_hook_result_t r=pm_device_resume_all();
    if(r==PM_HOOK_PENDING)return ctx->state;
    /* Optional-device restore errors are recorded, not allowed to trap UI. */
    ctx->transition_pending=0;ctx->resuming=0;
    ctx->state=PM_STATE_RUN;ctx->wake_latched=0;
    pm_controller_refresh_idle(ctx);
    return ctx->state;
}
pm_power_state_t pm_controller_step(pm_controller_t *ctx) {
    pm_policy_input_t in;
    pm_power_state_t next;
    pm_hook_result_t r;
    if(!ctx)return PM_STATE_RUN;
    if(ctx->resuming)return finish_resume(ctx);
    in.ui_active=ctx->ui_active;
    in.idle_timeout=(ctx->idle_timer_paused && !pm_sleep_timer_is_forced())?
    0u:pm_sleep_timer_expired();
    in.usb_or_dc_attached=ctx->usb_or_dc_attached;
    in.low_battery=ctx->low_battery;
    in.force_standby=ctx->force_standby;
    in.wake_event=ctx->wake_latched;
    in.post_deepsleep_hold=ctx->post_hold_ticks!=0u;
    next=pm_policy_next_state(ctx->state,&in);
    if(ctx->transition_pending && next==PM_STATE_RUN) {
        pm_device_abort_sequence();ctx->resuming=1;
        return finish_resume(ctx);
    }
    if(next!=ctx->state) {
        if(next==PM_STATE_RUN){ctx->resuming=1;return finish_resume(ctx);}
        r=pm_handle_transition(ctx->state,next);
        if(r==PM_HOOK_PENDING) {
            ctx->transition_pending=1;ctx->wake_latched=0;
            return ctx->state;
        }
        if(r==PM_HOOK_ERROR) {
            pm_device_abort_sequence();ctx->resuming=1;
            ctx->transition_pending=1;return finish_resume(ctx);
        }
        ctx->transition_pending=0;
    }
    if(ctx->state==PM_STATE_DEEPSLEEP && next==PM_STATE_SLEEP_PENDING)
        ctx->post_hold_ticks=PM_PENDING_HOLD_TICKS;
    else if(ctx->post_hold_ticks>0)--ctx->post_hold_ticks;
    ctx->state=next;ctx->wake_latched=0;return ctx->state;
}
