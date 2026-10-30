#include "kbdef.h"
#include "user_init.h"
#include "gpio.h"

void user_gpio_init();

void user_init()
{
    user_gpio_init();
}

void user_gpio_init()
{
    // RGB channels: outputs latched low with pull-ups, as stock leaves them (0xABBE)
    GPIO_DIR_WRITE(1, LED_P1_MASK);
    GPIO_DIR_WRITE(2, LED_P2_MASK);
    GPIO_DIR_WRITE(3, LED_P3_MASK);
    GPIO_PULLUP_WRITE(1, LED_P1_MASK);
    GPIO_PULLUP_WRITE(2, LED_P2_MASK);
    GPIO_PULLUP_WRITE(3, LED_P3_MASK);
    GPIO_WRITE(1, 0);
    GPIO_WRITE(2, 0);
    GPIO_WRITE(3, 0);

    GPIO_DIR_WRITE(0, (uint8_t)(KB_C18_P0_5 | KB_C19_P0_6 | KB_C20_P0_7));
    GPIO_DIR_WRITE(4, (uint8_t)(KB_C12_P4_0 | KB_C13_P4_2 | KB_C14_P4_3 | KB_C15_P4_5 | KB_C16_P4_6));
    GPIO_DIR_WRITE(5, (uint8_t)(KB_C8_P5_0 | KB_C9_P5_1 | KB_C10_P5_2 | KB_C11_P5_7));
    GPIO_DIR_WRITE(6, (uint8_t)(KB_C0_P6_0 | KB_C1_P6_1 | KB_C2_P6_2 | KB_C3_P6_3 | KB_C4_P6_4 | KB_C5_P6_5 | KB_C6_P6_6 | KB_C7_P6_7));
    GPIO_DIR_WRITE(7, (uint8_t)(KB_C17_P7_4));

    GPIO_PULLUP_WRITE(5, (uint8_t)(KB_R4_P5_3 | KB_R5_P5_4 | MODE_SW_A_P5_5 | MODE_SW_B_P5_6));
    GPIO_PULLUP_WRITE(7, (uint8_t)(KB_R0_P7_0 | KB_R1_P7_1 | KB_R2_P7_2 | KB_R3_P7_3));

#ifdef RF_ENABLED
    // bb_spi drives these open-drain: latch high, and leave PxCR to its per-cycle toggling.
    GPIO_HIGH(7, RF_BB_SPI_CS_MASK);
    GPIO_HIGH(4, RF_BB_SPI_SCK_MASK);
    GPIO_HIGH(0, (RF_BB_SPI_MOSI_MASK | RF_BB_SPI_MOT_MASK));
    GPIO_PULLUP_ON(0, (RF_BB_SPI_MISO_MASK | RF_BB_SPI_MOSI_MASK | RF_BB_SPI_MOT_MASK));
    GPIO_PULLUP_ON(4, (RF_BB_SPI_ACK_MASK | RF_BB_SPI_SCK_MASK));
    GPIO_PULLUP_ON(7, RF_BB_SPI_CS_MASK);
#endif
}
