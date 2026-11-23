#ifndef TEST_TIM_H
#define TEST_TIM_H
#include <stdint.h>
typedef struct {uint32_t EGR,ARR,CCR4,PSC,CNT;} TIM_TypeDef;
typedef struct {TIM_TypeDef *Instance;} TIM_HandleTypeDef;
extern TIM_TypeDef tim3_regs;
extern TIM_HandleTypeDef htim3;
#define TIM_CHANNEL_4 4u
#define TIM_EGR_UG 1u
#define HAL_OK 0u
#define __HAL_TIM_SET_PRESCALER(h,v) ((h)->Instance->PSC=(v))
#define __HAL_TIM_SET_AUTORELOAD(h,v) ((h)->Instance->ARR=(v))
#define __HAL_TIM_SET_COMPARE(h,ch,v) ((void)(ch),(h)->Instance->CCR4=(v))
#define __HAL_TIM_SET_COUNTER(h,v) ((h)->Instance->CNT=(v))
#define __HAL_TIM_GET_AUTORELOAD(h) ((h)->Instance->ARR)
static inline uint32_t HAL_TIM_PWM_Stop(TIM_HandleTypeDef*h,uint32_t c){(void)h;(void)c;return HAL_OK;}
static inline uint32_t HAL_TIM_PWM_Start(TIM_HandleTypeDef*h,uint32_t c){(void)h;(void)c;return HAL_OK;}
#endif
