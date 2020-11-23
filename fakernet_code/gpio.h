#include <inttypes.h>
#include <stdlib.h>
#define GPIO_0_BASE 0x0000
#define GPIO_1_BASE 0x1000
#define GPIO_2_BASE 0x3000
#define POWER_UP_GPIO_BASE 0x0000

uint32_t write_gpio_0_command(uint32_t *args);
uint32_t read_gpio_0_command(uint32_t *args);

uint32_t write_gpio_1_command(uint32_t *args);
uint32_t read_gpio_1_command(uint32_t *args);

uint32_t write_gpio_2_command(uint32_t *args);
uint32_t read_gpio_2_command(uint32_t *args);

uint32_t set_adc_power_command(uint32_t *args);
uint32_t set_preamp_power_command(uint32_t *args);
