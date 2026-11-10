#pragma once
#include "FreeRTOS.h"
int xTaskCreate(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle);
void vTaskDelete(TaskHandle_t task);
void vTaskSuspend(TaskHandle_t task);
void xTaskNotifyGive(TaskHandle_t task);
unsigned ulTaskNotifyTake(int clear,unsigned timeout);
void vTaskDelay(unsigned ticks);
