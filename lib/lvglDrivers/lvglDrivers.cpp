#include "lvglDrivers.h"
#include "lv_conf.h"
#include "stm32746g_discovery_lcd.h"
#include "stm32746g_discovery_ts.h"

static SemaphoreHandle_t lvglMutex;

bool lvglLock(TickType_t xBlockTime)
{
    if (xSemaphoreTake(lvglMutex, xBlockTime) == pdTRUE)
    {
        return true;
    }
    return false;
}

bool lvglUnlock()
{
    if (xSemaphoreGive(lvglMutex) == pdTRUE)
    {
        return true;
    }
    return false;
}

static void lvglTask(void *pvParameters)
{
    while (1)
    {
        xSemaphoreTake(lvglMutex, portMAX_DELAY);
        uint32_t time_till_next = lv_timer_handler();
        xSemaphoreGive(lvglMutex);
        vTaskDelay(pdMS_TO_TICKS(time_till_next));
    }
}

static void my_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t *buf = (uint32_t *)px_map;
    int32_t x, y;
    for (y = area->y1; y <= area->y2; y++)
    {
        for (x = area->x1; x <= area->x2; x++)
        {
            BSP_LCD_DrawPixel(x, y, *buf);
            buf++;
        }
    }
    lv_display_flush_ready(display);
}

static void my_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    TS_StateTypeDef TS_State;
    BSP_TS_GetState(&TS_State);

    if (TS_State.touchDetected != 0)
    {
        data->point.x = TS_State.touchX[0];
        data->point.y = TS_State.touchY[0];
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Start");

    Serial.println(">>> BSP_LCD_Init...");
    BSP_LCD_Init();
    Serial.println(">>> BSP_LCD_LayerDefaultInit...");
    BSP_LCD_LayerDefaultInit(0, LCD_FB_START_ADDRESS);
    Serial.println(">>> BSP_TS_Init...");
    BSP_TS_Init(480, 272);
    Serial.println(">>> xSemaphoreCreateMutex...");
    lvglMutex = xSemaphoreCreateMutex();
    Serial.println(">>> lv_init...");
    lv_init();
    Serial.println(">>> lv_log_register...");
    lv_log_register_print_cb([](lv_log_level_t level, const char *buf) {
        Serial.printf("%s", buf);
    });
    Serial.println(">>> lv_display_create...");
    lv_display_t *display = lv_display_create(480, 272);
    Serial.println(">>> lv_display_set_flush_cb...");
    lv_display_set_flush_cb(display, my_flush_cb);
    Serial.println(">>> lv_display_set_buffers...");
    static uint32_t buf[480 * 272 / 10];
    lv_display_set_buffers(display, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    Serial.println(">>> lv_indev_create...");
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_read_cb);
    Serial.println(">>> lv_tick_set_cb...");
    lv_tick_set_cb(xTaskGetTickCount);
    Serial.println(">>> mySetup...");
    mySetup();
    Serial.println(">>> xTaskCreate lvglTask...");
    xTaskCreate(lvglTask, NULL, 1024, NULL, osPriorityNormal, NULL);
    Serial.println(">>> xTaskCreate myTask...");
    xTaskCreate(myTask, NULL, 1024, NULL, osPriorityNormal, NULL);
    Serial.println(">>> vTaskStartScheduler...");
    vTaskStartScheduler();
    Serial.println("Insufficient RAM");
    while (1);
}