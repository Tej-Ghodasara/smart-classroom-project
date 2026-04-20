#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Define the pin you want to blink
#define BLINK_GPIO 2

// Create a tag for logging so we can see it in the terminal
static const char *TAG = "BLINK_EXAMPLE";

void app_main(void)
{
    // 1. Initialize the pin
    ESP_LOGI(TAG, "Configuring GPIO Pin %d as Output", BLINK_GPIO);
    gpio_reset_pin(BLINK_GPIO);
    
    // 2. Set the pin direction to Output
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    // Variable to keep track of the LED state (0 = OFF, 1 = ON)
    int led_state = 0;

    // 3. Infinite loop to blink the LED
    while (1) {
        ESP_LOGI(TAG, "Turning the LED %s!", led_state == 1 ? "ON" : "OFF");
        
        // Set the GPIO level
        gpio_set_level(BLINK_GPIO, led_state);
        
        // Toggle the state for the next loop
        led_state = !led_state;
        
        // Delay for 1000 milliseconds (1 second)
        // vTaskDelay expects 'ticks', so we divide our MS by the portTICK_PERIOD_MS
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}