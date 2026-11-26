#pragma once
#include "FreeRTOS.h"
#include "task.h"
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t semaphore,unsigned timeout);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
