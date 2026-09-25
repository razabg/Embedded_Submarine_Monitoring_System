/*
 * sonarled.c - LNC Object Detection breathing red LED
 * See sonarled.h for the public API and the design overview.
 */
#include "sonarled.h"
#include "cmsis_os.h"
#include <stdbool.h>

/* TICK_MS is how often the task wakes to nudge the duty cycle; STEP is
 * how much each nudge moves it, sized so the full 0..MAX_DUTY sweep
 * takes BREATH_HALF_MS. MAX_DUTY matches TIM8's ARR (Period=999), the
 * top of its PWM duty range. */
#define TICK_MS         30u
#define BREATH_HALF_MS  2000u
#define MAX_DUTY        999u
#define STEP            (MAX_DUTY / (BREATH_HALF_MS / TICK_MS))

struct SonarLed {
    TIM_HandleTypeDef *pwm_timer;
    uint32_t channel;
    osThreadId_t task_handle;
    volatile bool active; /* written by Start/Stop (Event's caller task), read by sonarled_task */
    uint32_t duty;
    bool rising;
};

static struct SonarLed g_sonarled;

static void sonarled_task(void *argument)
{
    (void)argument;
    uint32_t tick = osKernelGetTickCount();

    for (;;) {
        if (g_sonarled.active) {
            if (g_sonarled.rising) {
                if (g_sonarled.duty + STEP >= MAX_DUTY) {
                    g_sonarled.duty = MAX_DUTY;
                    g_sonarled.rising = false;
                } else {
                    g_sonarled.duty += STEP;
                }
            } else {
                if (g_sonarled.duty < STEP) {
                    g_sonarled.duty = 0;
                    g_sonarled.rising = true;
                } else {
                    g_sonarled.duty -= STEP;
                }
            }
            __HAL_TIM_SET_COMPARE(g_sonarled.pwm_timer, g_sonarled.channel, g_sonarled.duty);
        }

        tick += TICK_MS;
        osDelayUntil(tick); /* == FreeRTOS vTaskDelayUntil() */
    }
}

SonarLed *SonarLed_Create(TIM_HandleTypeDef *pwm_timer, uint32_t channel)
{
    const osThreadAttr_t task_attr = {
        .name = "sonarLedTask",
        .stack_size = 256 * 4,
        .priority = osPriorityNormal,
    };

    if (pwm_timer == NULL) {
        return NULL;
    }

    g_sonarled.pwm_timer = pwm_timer;
    g_sonarled.channel = channel;
    g_sonarled.active = false;
    g_sonarled.duty = 0;
    g_sonarled.rising = true;

    g_sonarled.task_handle = osThreadNew(sonarled_task, NULL, &task_attr);
    if (g_sonarled.task_handle == NULL) {
        return NULL;
    }

    return &g_sonarled;
}

void SonarLed_Start(SonarLed *h)
{
    if (h == NULL) {
        return;
    }
    h->duty = 0;
    h->rising = true;
    __HAL_TIM_SET_COMPARE(h->pwm_timer, h->channel, 0);
    HAL_TIM_PWM_Start(h->pwm_timer, h->channel);
    h->active = true;
}

void SonarLed_Stop(SonarLed *h)
{
    if (h == NULL) {
        return;
    }
    h->active = false;
    HAL_TIM_PWM_Stop(h->pwm_timer, h->channel);
}
