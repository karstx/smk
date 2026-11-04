#include "kbdef.h"
#include "gpio.h"
#include "user_matrix.h"

void user_matrix_cols_deselect_all(void)
{
    GPIO_HIGH(4, KB_C_P4_MASK);
    GPIO_HIGH(5, KB_C_P5_MASK);
    GPIO_HIGH(6, KB_C_P6_MASK);
}

void user_matrix_scan_pre(void)
{
    GPIO_OUTPUT(4, KB_C_P4_MASK);
    GPIO_OUTPUT(5, KB_C_P5_MASK);
    GPIO_OUTPUT(6, KB_C_P6_MASK);
    user_matrix_cols_deselect_all();
}

void user_matrix_scan_post(void)
{
    user_matrix_cols_deselect_all();
}

void user_matrix_col_select(uint8_t col)
{
    switch (col) {
        case 0: KB_C0 = 0; break;
        case 1: KB_C1 = 0; break;
        case 2: KB_C2 = 0; break;
        case 3: KB_C3 = 0; break;
        case 4: KB_C4 = 0; break;
        case 5: KB_C5 = 0; break;
        case 6: KB_C6 = 0; break;
        case 7: KB_C7 = 0; break;
        case 8: KB_C8 = 0; break;
        case 9: KB_C9 = 0; break;
        case 10: KB_C10 = 0; break;
        case 11: KB_C11 = 0; break;
        case 12: KB_C12 = 0; break;
        case 13: KB_C13 = 0; break;
        case 14: KB_C14 = 0; break;
        case 15: KB_C15 = 0; break;
        case 16: KB_C16 = 0; break;
    }
}

void user_matrix_col_deselect(uint8_t col)
{
    switch (col) {
        case 0: KB_C0 = 1; break;
        case 1: KB_C1 = 1; break;
        case 2: KB_C2 = 1; break;
        case 3: KB_C3 = 1; break;
        case 4: KB_C4 = 1; break;
        case 5: KB_C5 = 1; break;
        case 6: KB_C6 = 1; break;
        case 7: KB_C7 = 1; break;
        case 8: KB_C8 = 1; break;
        case 9: KB_C9 = 1; break;
        case 10: KB_C10 = 1; break;
        case 11: KB_C11 = 1; break;
        case 12: KB_C12 = 1; break;
        case 13: KB_C13 = 1; break;
        case 14: KB_C14 = 1; break;
        case 15: KB_C15 = 1; break;
        case 16: KB_C16 = 1; break;
    }
}

uint8_t user_matrix_read_rows(void)
{
    return (uint8_t)((P7 & 0x0Fu) | ((P5 & 0x18u) << 1) | 0xC0u);
}

void user_matrix_sinks_off(void) {}
