#include "display.hpp"
#include <cstdint>
#include "tm1750_service.hpp"

#define C1 1
#define C2 0
#define C3 2
#define C4 3
#define C5 4
#define C6 5
#define P7 0
#define P8 1
#define P9 2
#define P10 3
#define P11 4
#define P12 5
#define P13 6
#define P14 7
#define P15 8
#define P16 9
#define P17 10
#define P18 11
#define P19 12
#define P20 13
#define P21 14
#define P22 15
#define P23 16
#define P24 17
#define P25 18
#define P26 19
#define P27 20
#define P28 21
#define P29 22
#define P30 23
#define P31 24
#define P32 25
#define P33 26
#define P34 27

typedef struct PixInfo {
    uint8_t seg;
    uint8_t com;
} PixInfo_t;

typedef struct NumSegInfo {
    struct {
        uint16_t a;
        uint16_t b;
        uint16_t c;
        uint16_t d;
        uint16_t e;
        uint16_t f;
        uint16_t g;
    } pix_index;
} NumSegInfo_t;

typedef struct Bitmap {
    char chr;
    uint8_t bitmap;
} Bitmap_t;

static const PixInfo_t pix_infos[] = {
    [Display::PIX_DATA_TRANSMITTING] = {.seg = P33, .com = C1},    /* T20 */
    [Display::PIX_DATA_SYM] = {.seg = P33, .com = C2}, /* T21 */
    [Display::PIX_DATA_LEVEL_0] = {.seg = P33, .com = C3}, /* T22 */
    [Display::PIX_DATA_LEVEL_1] = {.seg = P33, .com = C4}, /* T23 */
    [Display::PIX_DATA_LEVEL_2] = {.seg = P33, .com = C5}, /* T24 */
    [Display::PIX_DATA_LEVEL_3] = {.seg = P33, .com = C6}, /* T25 */
    [Display::PIX_MODE_MANUAL] = {.seg = P32, .com = C4},  /* T26 */
    [Display::PIX_MODE_AUTO] = {.seg = P32, .com = C3},    /* T27 */
    [Display::PIX_MODE_TIMING] = {.seg = P32, .com = C2},  /* T28 */
    [Display::PIX_STATUS_RAIN] = {.seg = P32, .com = C1},  /* T29 */
    [Display::PIX_ALERT] = {.seg = P31, .com = C1},    /* T30 */
    [Display::PIX_BATTERY_0] = {.seg = P31, .com = C5},    /* T31 */
    [Display::PIX_BATTERY_1] = {.seg = P31, .com = C2},    /* T32 */
    [Display::PIX_BATTERY_2] = {.seg = P31, .com = C3},    /* T33 */
    [Display::PIX_BATTERY_3] = {.seg = P31, .com = C4},    /* T34 */
    [Display::PIX_BAR1] = {.seg = P32, .com = C5}, /* T19 */
    [Display::PIX_BAR2] = {.seg = P17, .com = C2}, /* T5 */
    [Display::PIX_OPENING_PERCENTAGE_TEXT] = {.seg = P32, .com = C6},  /* T16 */
    [Display::PIX_MOTOR_STATUS_OPENING] = {.seg = P34, .com = C6}, /* T18 */
    [Display::PIX_MOTOR_STATUS_CLOSING] = {.seg = P34, .com = C5}, /* T17 */
    [Display::PIX_OPENING_PERCENTAGE_S1_A] = {.seg = P30, .com = C6},  /* 11A */
    [Display::PIX_OPENING_PERCENTAGE_S1_B] = {.seg = P30, .com = C5},  /* 11B */
    [Display::PIX_OPENING_PERCENTAGE_S1_C] = {.seg = P30, .com = C2},  /* 11C */
    [Display::PIX_OPENING_PERCENTAGE_S1_D] = {.seg = P7, .com = C1},  /* 11D */
    [Display::PIX_OPENING_PERCENTAGE_S1_E] = {.seg = P30, .com = C1},  /* 11E */
    [Display::PIX_OPENING_PERCENTAGE_S1_F] = {.seg = P30, .com = C4},  /* 11F */
    [Display::PIX_OPENING_PERCENTAGE_S1_G] = {.seg = P30, .com = C3},  /* 11G */
    [Display::PIX_OPENING_PERCENTAGE_S2_A] = {.seg = P29, .com = C6},  /* 12A */
    [Display::PIX_OPENING_PERCENTAGE_S2_B] = {.seg = P29, .com = C5},  /* 12B */
    [Display::PIX_OPENING_PERCENTAGE_S2_C] = {.seg = P29, .com = C2},  /* 12C */
    [Display::PIX_OPENING_PERCENTAGE_S2_D] = {.seg = P8, .com = C1},  /* 12D */
    [Display::PIX_OPENING_PERCENTAGE_S2_E] = {.seg = P29, .com = C1},  /* 12E */
    [Display::PIX_OPENING_PERCENTAGE_S2_F] = {.seg = P29, .com = C4},  /* 12F */
    [Display::PIX_OPENING_PERCENTAGE_S2_G] = {.seg = P29, .com = C3},  /* 12G */
    [Display::PIX_OPENING_PERCENTAGE_S3_A] = {.seg = P28, .com = C6},  /* 13A */
    [Display::PIX_OPENING_PERCENTAGE_S3_B] = {.seg = P28, .com = C5},  /* 13B */
    [Display::PIX_OPENING_PERCENTAGE_S3_C] = {.seg = P28, .com = C2},  /* 13C */
    [Display::PIX_OPENING_PERCENTAGE_S3_D] = {.seg = P9, .com = C1},  /* 13D */
    [Display::PIX_OPENING_PERCENTAGE_S3_E] = {.seg = P28, .com = C1},  /* 13E */
    [Display::PIX_OPENING_PERCENTAGE_S3_F] = {.seg = P28, .com = C4},  /* 13F */
    [Display::PIX_OPENING_PERCENTAGE_S3_G] = {.seg = P28, .com = C3},  /* 13G */
    [Display::PIX_OPENING_PERCENTAGE_SYM_PERCENTAGE] = {.seg = P10, .com = C1},    /* T12 */
    [Display::PIX_TEMP_SYM_SUBZERO] = {.seg = P11, .com = C1}, /* T13 */
    [Display::PIX_TEMP_S1_A] = {.seg = P27, .com = C6},    /* 14A */
    [Display::PIX_TEMP_S1_B] = {.seg = P27, .com = C5},    /* 14B */
    [Display::PIX_TEMP_S1_C] = {.seg = P27, .com = C2},    /* 14C */
    [Display::PIX_TEMP_S1_D] = {.seg = P12, .com = C1},    /* 14D */
    [Display::PIX_TEMP_S1_E] = {.seg = P27, .com = C1},    /* 14E */
    [Display::PIX_TEMP_S1_F] = {.seg = P27, .com = C4},    /* 14F */
    [Display::PIX_TEMP_S1_G] = {.seg = P27, .com = C3},    /* 14G */
    [Display::PIX_TEMP_S2_A] = {.seg = P26, .com = C6},    /* 15A */
    [Display::PIX_TEMP_S2_B] = {.seg = P26, .com = C5},    /* 15B */
    [Display::PIX_TEMP_S2_C] = {.seg = P26, .com = C2},    /* 15C */
    [Display::PIX_TEMP_S2_D] = {.seg = P13, .com = C1},    /* 15D */
    [Display::PIX_TEMP_S2_E] = {.seg = P26, .com = C1},    /* 15E */
    [Display::PIX_TEMP_S2_F] = {.seg = P26, .com = C4},    /* 15F */
    [Display::PIX_TEMP_S2_G] = {.seg = P26, .com = C3},    /* 15G */
    [Display::PIX_TEMP_S3_A] = {.seg = P25, .com = C6},    /* 16A */
    [Display::PIX_TEMP_S3_B] = {.seg = P25, .com = C5},    /* 16B */
    [Display::PIX_TEMP_S3_C] = {.seg = P25, .com = C2},    /* 16C */
    [Display::PIX_TEMP_S3_D] = {.seg = P15, .com = C1},    /* 16D */
    [Display::PIX_TEMP_S3_E] = {.seg = P25, .com = C1},    /* 16E */
    [Display::PIX_TEMP_S3_F] = {.seg = P25, .com = C4},    /* 16F */
    [Display::PIX_TEMP_S3_G] = {.seg = P25, .com = C3},    /* 16G */
    [Display::PIX_TEMP_DOT] = {.seg = P14, .com = C1}, /* T14 */
    [Display::PIX_TEMP_SYM] = {.seg = P16, .com = C1}, /* T15 */
    [Display::PIX_HUMI_S1_A] = {.seg = P24, .com = C6},    /* 17A */
    [Display::PIX_HUMI_S1_B] = {.seg = P24, .com = C5},    /* 17B */
    [Display::PIX_HUMI_S1_C] = {.seg = P24, .com = C2},    /* 17C */
    [Display::PIX_HUMI_S1_D] = {.seg = P17, .com = C1},    /* 17D */
    [Display::PIX_HUMI_S1_E] = {.seg = P24, .com = C1},    /* 17E */
    [Display::PIX_HUMI_S1_F] = {.seg = P24, .com = C4},    /* 17F */
    [Display::PIX_HUMI_S1_G] = {.seg = P24, .com = C3},    /* 17G */
    [Display::PIX_HUMI_S2_A] = {.seg = P23, .com = C6},    /* 18A */
    [Display::PIX_HUMI_S2_B] = {.seg = P23, .com = C5},    /* 18B */
    [Display::PIX_HUMI_S2_C] = {.seg = P23, .com = C2},    /* 18C */
    [Display::PIX_HUMI_S2_D] = {.seg = P18, .com = C1},    /* 18D */
    [Display::PIX_HUMI_S2_E] = {.seg = P23, .com = C1},    /* 18E */
    [Display::PIX_HUMI_S2_F] = {.seg = P23, .com = C4},    /* 18F */
    [Display::PIX_HUMI_S2_G] = {.seg = P23, .com = C3},    /* 18G */
    [Display::PIX_HUMI_S3_A] = {.seg = P22, .com = C6},    /* 19A */
    [Display::PIX_HUMI_S3_B] = {.seg = P22, .com = C5},    /* 19B */
    [Display::PIX_HUMI_S3_C] = {.seg = P22, .com = C2},    /* 19C */
    [Display::PIX_HUMI_S3_D] = {.seg = P20, .com = C1},    /* 19D */
    [Display::PIX_HUMI_S3_E] = {.seg = P22, .com = C1},    /* 19E */
    [Display::PIX_HUMI_S3_F] = {.seg = P22, .com = C4},    /* 19F */
    [Display::PIX_HUMI_S3_G] = {.seg = P22, .com = C3},    /* 19G */
    [Display::PIX_HUMI_DOT] = {.seg = P19, .com = C1}, /* T10 */
    [Display::PIX_HUMI_SYM] = {.seg = P21, .com = C1}, /* T11 */
    [Display::PIX_SN_SYM] = {.seg = P8, .com = C2},   /* T1 */
    [Display::PIX_DATE_YEAR_S1_A] = {.seg = P7, .com = C2},   /* 1A */
    [Display::PIX_DATE_YEAR_S1_B] = {.seg = P8, .com = C4},   /* 1B */
    [Display::PIX_DATE_YEAR_S1_C] = {.seg = P8, .com = C6},   /* 1C */
    [Display::PIX_DATE_YEAR_S1_D] = {.seg = P7, .com = C6},   /* 1D */
    [Display::PIX_DATE_YEAR_S1_E] = {.seg = P7, .com = C5},   /* 1E */
    [Display::PIX_DATE_YEAR_S1_F] = {.seg = P7, .com = C3},   /* 1F */
    [Display::PIX_DATE_YEAR_S1_G] = {.seg = P7, .com = C4},   /* 1G */
    [Display::PIX_DATE_YEAR_S2_A] = {.seg = P9, .com = C2},   /* 2A */
    [Display::PIX_DATE_YEAR_S2_B] = {.seg = P9, .com = C3},   /* 2B */
    [Display::PIX_DATE_YEAR_S2_C] = {.seg = P9, .com = C5},   /* 2C */
    [Display::PIX_DATE_YEAR_S2_D] = {.seg = P9, .com = C6},   /* 2D */
    [Display::PIX_DATE_YEAR_S2_E] = {.seg = P8, .com = C5},   /* 2E */
    [Display::PIX_DATE_YEAR_S2_F] = {.seg = P8, .com = C3},   /* 2F */
    [Display::PIX_DATE_YEAR_S2_G] = {.seg = P9, .com = C4},   /* 2G */
    [Display::PIX_DATE_YEAR_S3_A] = {.seg = P10, .com = C2},   /* 3A */
    [Display::PIX_DATE_YEAR_S3_B] = {.seg = P11, .com = C4},   /* 3B */
    [Display::PIX_DATE_YEAR_S3_C] = {.seg = P11, .com = C6},   /* 3C */
    [Display::PIX_DATE_YEAR_S3_D] = {.seg = P10, .com = C6},   /* 3D */
    [Display::PIX_DATE_YEAR_S3_E] = {.seg = P10, .com = C5},   /* 3E */
    [Display::PIX_DATE_YEAR_S3_F] = {.seg = P10, .com = C3},   /* 3F */
    [Display::PIX_DATE_YEAR_S3_G] = {.seg = P10, .com = C4},   /* 3G */
    [Display::PIX_DATE_YEAR_S4_A] = {.seg = P12, .com = C2},   /* 4A */
    [Display::PIX_DATE_YEAR_S4_B] = {.seg = P12, .com = C3},   /* 4B */
    [Display::PIX_DATE_YEAR_S4_C] = {.seg = P12, .com = C5},   /* 4C */
    [Display::PIX_DATE_YEAR_S4_D] = {.seg = P12, .com = C6},   /* 4D */
    [Display::PIX_DATE_YEAR_S4_E] = {.seg = P11, .com = C5},   /* 4E */
    [Display::PIX_DATE_YEAR_S4_F] = {.seg = P11, .com = C3},   /* 4F */
    [Display::PIX_DATE_YEAR_S4_G] = {.seg = P12, .com = C4},   /* 4G */
    [Display::PIX_DATE_YEAR_S5_A] = {.seg = P13, .com = C2},   /* 5A */
    [Display::PIX_DATE_YEAR_S5_B] = {.seg = P14, .com = C4},   /* 5B */
    [Display::PIX_DATE_YEAR_S5_C] = {.seg = P14, .com = C6},   /* 5C */
    [Display::PIX_DATE_YEAR_S5_D] = {.seg = P13, .com = C6},   /* 5D */
    [Display::PIX_DATE_YEAR_S5_E] = {.seg = P13, .com = C5},   /* 5E */
    [Display::PIX_DATE_YEAR_S5_F] = {.seg = P13, .com = C3},   /* 5F */
    [Display::PIX_DATE_YEAR_S5_G] = {.seg = P13, .com = C4},   /* 5G */
    [Display::PIX_DATE_YEAR_S6_A] = {.seg = P15, .com = C2},   /* 6A */
    [Display::PIX_DATE_YEAR_S6_B] = {.seg = P15, .com = C3},   /* 6B */
    [Display::PIX_DATE_YEAR_S6_C] = {.seg = P15, .com = C5},   /* 6C */
    [Display::PIX_DATE_YEAR_S6_D] = {.seg = P15, .com = C6},   /* 6D */
    [Display::PIX_DATE_YEAR_S6_E] = {.seg = P14, .com = C5},   /* 6E */
    [Display::PIX_DATE_YEAR_S6_F] = {.seg = P14, .com = C3},   /* 6F */
    [Display::PIX_DATE_YEAR_S6_G] = {.seg = P15, .com = C4},   /* 6G */
    [Display::PIX_DATE_YEAR_BAR_1] = {.seg = P11, .com = C2},  /* T2 */
    [Display::PIX_DATE_YEAR_BAR_2] = {.seg = P14, .com = C2},  /* T3 */
    [Display::PIX_TIME_S1_A] = {.seg = P16, .com = C2},    /* 7A */
    [Display::PIX_TIME_S1_B] = {.seg = P17, .com = C4},    /* 7B */
    [Display::PIX_TIME_S1_C] = {.seg = P17, .com = C6},    /* 7C */
    [Display::PIX_TIME_S1_D] = {.seg = P16, .com = C6},    /* 7D */
    [Display::PIX_TIME_S1_E] = {.seg = P16, .com = C5},    /* 7E */
    [Display::PIX_TIME_S1_F] = {.seg = P16, .com = C3},    /* 7F */
    [Display::PIX_TIME_S1_G] = {.seg = P16, .com = C4},    /* 7G */
    [Display::PIX_TIME_S2_A] = {.seg = P18, .com = C2},    /* 8A */
    [Display::PIX_TIME_S2_B] = {.seg = P18, .com = C3},    /* 8B */
    [Display::PIX_TIME_S2_C] = {.seg = P18, .com = C5},    /* 8C */
    [Display::PIX_TIME_S2_D] = {.seg = P18, .com = C6},    /* 8D */
    [Display::PIX_TIME_S2_E] = {.seg = P17, .com = C5},    /* 8E */
    [Display::PIX_TIME_S2_F] = {.seg = P17, .com = C3},    /* 8F */
    [Display::PIX_TIME_S2_G] = {.seg = P18, .com = C4},    /* 8G */
    [Display::PIX_TIME_S3_A] = {.seg = P19, .com = C2},    /* 9A */
    [Display::PIX_TIME_S3_B] = {.seg = P20, .com = C4},    /* 9B */
    [Display::PIX_TIME_S3_C] = {.seg = P20, .com = C6},    /* 9C */
    [Display::PIX_TIME_S3_D] = {.seg = P19, .com = C6},    /* 9D */
    [Display::PIX_TIME_S3_E] = {.seg = P19, .com = C5},    /* 9E */
    [Display::PIX_TIME_S3_F] = {.seg = P19, .com = C3},    /* 9F */
    [Display::PIX_TIME_S3_G] = {.seg = P19, .com = C4},    /* 9G */
    [Display::PIX_TIME_S4_A] = {.seg = P21, .com = C2},    /* 10A */
    [Display::PIX_TIME_S4_B] = {.seg = P21, .com = C3},    /* 10B */
    [Display::PIX_TIME_S4_C] = {.seg = P21, .com = C5},    /* 10C */
    [Display::PIX_TIME_S4_D] = {.seg = P21, .com = C6},    /* 10D */
    [Display::PIX_TIME_S4_E] = {.seg = P20, .com = C5},    /* 10E */
    [Display::PIX_TIME_S4_F] = {.seg = P20, .com = C3},    /* 10F */
    [Display::PIX_TIME_S4_G] = {.seg = P21, .com = C4},    /* 10G */
    [Display::PIX_TIME_DOT] = {.seg = P20, .com = C2}, /* T4 */
    [Display::PIX_WATCHING_SYM] = {.seg = P34, .com = C4},    /* T6 */
    [Display::PIX_OK_SYM] = {.seg = P34, .com = C3},    /* T7 */
    [Display::PIX_ERR_SYM] = {.seg = P34, .com = C2},    /* T8 */
    [Display::PIX_OTA_SYM] = {.seg = P34, .com = C1}, /* T9 */
};

static const NumSegInfo_t num_seg_infos[] = {
    [Display::NUM_SEG_OPENING_PERCENTAGE_S1] = {
        .pix_index = {
            .a = Display::PIX_OPENING_PERCENTAGE_S1_A,
            .b = Display::PIX_OPENING_PERCENTAGE_S1_B,
            .c = Display::PIX_OPENING_PERCENTAGE_S1_C,
            .d = Display::PIX_OPENING_PERCENTAGE_S1_D,
            .e = Display::PIX_OPENING_PERCENTAGE_S1_E,
            .f = Display::PIX_OPENING_PERCENTAGE_S1_F,
            .g = Display::PIX_OPENING_PERCENTAGE_S1_G,
        }
    },
    [Display::NUM_SEG_OPENING_PERCENTAGE_S2] = {
        .pix_index = {
            .a = Display::PIX_OPENING_PERCENTAGE_S2_A,
            .b = Display::PIX_OPENING_PERCENTAGE_S2_B,
            .c = Display::PIX_OPENING_PERCENTAGE_S2_C,
            .d = Display::PIX_OPENING_PERCENTAGE_S2_D,
            .e = Display::PIX_OPENING_PERCENTAGE_S2_E,
            .f = Display::PIX_OPENING_PERCENTAGE_S2_F,
            .g = Display::PIX_OPENING_PERCENTAGE_S2_G,
        }
    },
    [Display::NUM_SEG_OPENING_PERCENTAGE_S3] = {
        .pix_index = {
            .a = Display::PIX_OPENING_PERCENTAGE_S3_A,
            .b = Display::PIX_OPENING_PERCENTAGE_S3_B,
            .c = Display::PIX_OPENING_PERCENTAGE_S3_C,
            .d = Display::PIX_OPENING_PERCENTAGE_S3_D,
            .e = Display::PIX_OPENING_PERCENTAGE_S3_E,
            .f = Display::PIX_OPENING_PERCENTAGE_S3_F,
            .g = Display::PIX_OPENING_PERCENTAGE_S3_G,
        }
    },
    [Display::NUM_SEG_TEMP_S1] = {
        .pix_index = {
            .a = Display::PIX_TEMP_S1_A,
            .b = Display::PIX_TEMP_S1_B,
            .c = Display::PIX_TEMP_S1_C,
            .d = Display::PIX_TEMP_S1_D,
            .e = Display::PIX_TEMP_S1_E,
            .f = Display::PIX_TEMP_S1_F,
            .g = Display::PIX_TEMP_S1_G,
        }
    },
    [Display::NUM_SEG_TEMP_S2] = {
        .pix_index = {
            .a = Display::PIX_TEMP_S2_A,
            .b = Display::PIX_TEMP_S2_B,
            .c = Display::PIX_TEMP_S2_C,
            .d = Display::PIX_TEMP_S2_D,
            .e = Display::PIX_TEMP_S2_E,
            .f = Display::PIX_TEMP_S2_F,
            .g = Display::PIX_TEMP_S2_G,
        }
    },
    [Display::NUM_SEG_TEMP_S3] = {
        .pix_index = {
            .a = Display::PIX_TEMP_S3_A,
            .b = Display::PIX_TEMP_S3_B,
            .c = Display::PIX_TEMP_S3_C,
            .d = Display::PIX_TEMP_S3_D,
            .e = Display::PIX_TEMP_S3_E,
            .f = Display::PIX_TEMP_S3_F,
            .g = Display::PIX_TEMP_S3_G,
        }
    },
    [Display::NUM_SEG_HUMI_S1] = {
        .pix_index = {
            .a = Display::PIX_HUMI_S1_A,
            .b = Display::PIX_HUMI_S1_B,
            .c = Display::PIX_HUMI_S1_C,
            .d = Display::PIX_HUMI_S1_D,
            .e = Display::PIX_HUMI_S1_E,
            .f = Display::PIX_HUMI_S1_F,
            .g = Display::PIX_HUMI_S1_G,
        }
    },
    [Display::NUM_SEG_HUMI_S2] = {
        .pix_index = {
            .a = Display::PIX_HUMI_S2_A,
            .b = Display::PIX_HUMI_S2_B,
            .c = Display::PIX_HUMI_S2_C,
            .d = Display::PIX_HUMI_S2_D,
            .e = Display::PIX_HUMI_S2_E,
            .f = Display::PIX_HUMI_S2_F,
            .g = Display::PIX_HUMI_S2_G,
        }
    },
    [Display::NUM_SEG_HUMI_S3] = {
        .pix_index = {
            .a = Display::PIX_HUMI_S3_A,
            .b = Display::PIX_HUMI_S3_B,
            .c = Display::PIX_HUMI_S3_C,
            .d = Display::PIX_HUMI_S3_D,
            .e = Display::PIX_HUMI_S3_E,
            .f = Display::PIX_HUMI_S3_F,
            .g = Display::PIX_HUMI_S3_G,
        }
    },
    [Display::NUM_SEG_DATE_YEAR_S1] = {
        .pix_index = {
            .a = Display::PIX_DATE_YEAR_S1_A,
            .b = Display::PIX_DATE_YEAR_S1_B,
            .c = Display::PIX_DATE_YEAR_S1_C,
            .d = Display::PIX_DATE_YEAR_S1_D,
            .e = Display::PIX_DATE_YEAR_S1_E,
            .f = Display::PIX_DATE_YEAR_S1_F,
            .g = Display::PIX_DATE_YEAR_S1_G,
        }
    },
    [Display::NUM_SEG_DATE_YEAR_S2] = {
        .pix_index = {
            .a = Display::PIX_DATE_YEAR_S2_A,
            .b = Display::PIX_DATE_YEAR_S2_B,
            .c = Display::PIX_DATE_YEAR_S2_C,
            .d = Display::PIX_DATE_YEAR_S2_D,
            .e = Display::PIX_DATE_YEAR_S2_E,
            .f = Display::PIX_DATE_YEAR_S2_F,
            .g = Display::PIX_DATE_YEAR_S2_G,
        }
    },
    [Display::NUM_SEG_DATE_YEAR_S3] = {
        .pix_index = {
            .a = Display::PIX_DATE_YEAR_S3_A,
            .b = Display::PIX_DATE_YEAR_S3_B,
            .c = Display::PIX_DATE_YEAR_S3_C,
            .d = Display::PIX_DATE_YEAR_S3_D,
            .e = Display::PIX_DATE_YEAR_S3_E,
            .f = Display::PIX_DATE_YEAR_S3_F,
            .g = Display::PIX_DATE_YEAR_S3_G,
        }
    },
    [Display::NUM_SEG_DATE_YEAR_S4] = {
        .pix_index = {
            .a = Display::PIX_DATE_YEAR_S4_A,
            .b = Display::PIX_DATE_YEAR_S4_B,
            .c = Display::PIX_DATE_YEAR_S4_C,
            .d = Display::PIX_DATE_YEAR_S4_D,
            .e = Display::PIX_DATE_YEAR_S4_E,
            .f = Display::PIX_DATE_YEAR_S4_F,
            .g = Display::PIX_DATE_YEAR_S4_G,
        }
    },
    [Display::NUM_SEG_DATE_YEAR_S5] = {
        .pix_index = {
            .a = Display::PIX_DATE_YEAR_S5_A,
            .b = Display::PIX_DATE_YEAR_S5_B,
            .c = Display::PIX_DATE_YEAR_S5_C,
            .d = Display::PIX_DATE_YEAR_S5_D,
            .e = Display::PIX_DATE_YEAR_S5_E,
            .f = Display::PIX_DATE_YEAR_S5_F,
            .g = Display::PIX_DATE_YEAR_S5_G,
        }
    },
    [Display::NUM_SEG_DATE_YEAR_S6] = {
        .pix_index = {
            .a = Display::PIX_DATE_YEAR_S6_A,
            .b = Display::PIX_DATE_YEAR_S6_B,
            .c = Display::PIX_DATE_YEAR_S6_C,
            .d = Display::PIX_DATE_YEAR_S6_D,
            .e = Display::PIX_DATE_YEAR_S6_E,
            .f = Display::PIX_DATE_YEAR_S6_F,
            .g = Display::PIX_DATE_YEAR_S6_G,
        }
    },
    [Display::NUM_SEG_TIME_S1] = {
        .pix_index = {
            .a = Display::PIX_TIME_S1_A,
            .b = Display::PIX_TIME_S1_B,
            .c = Display::PIX_TIME_S1_C,
            .d = Display::PIX_TIME_S1_D,
            .e = Display::PIX_TIME_S1_E,
            .f = Display::PIX_TIME_S1_F,
            .g = Display::PIX_TIME_S1_G,
        }
    },
    [Display::NUM_SEG_TIME_S2] = {
        .pix_index = {
            .a = Display::PIX_TIME_S2_A,
            .b = Display::PIX_TIME_S2_B,
            .c = Display::PIX_TIME_S2_C,
            .d = Display::PIX_TIME_S2_D,
            .e = Display::PIX_TIME_S2_E,
            .f = Display::PIX_TIME_S2_F,
            .g = Display::PIX_TIME_S2_G,
        }
    },
    [Display::NUM_SEG_TIME_S3] = {
        .pix_index = {
            .a = Display::PIX_TIME_S3_A,
            .b = Display::PIX_TIME_S3_B,
            .c = Display::PIX_TIME_S3_C,
            .d = Display::PIX_TIME_S3_D,
            .e = Display::PIX_TIME_S3_E,
            .f = Display::PIX_TIME_S3_F,
            .g = Display::PIX_TIME_S3_G,
        }
    },
    [Display::NUM_SEG_TIME_S4] = {
        .pix_index = {
            .a = Display::PIX_TIME_S4_A,
            .b = Display::PIX_TIME_S4_B,
            .c = Display::PIX_TIME_S4_C,
            .d = Display::PIX_TIME_S4_D,
            .e = Display::PIX_TIME_S4_E,
            .f = Display::PIX_TIME_S4_F,
            .g = Display::PIX_TIME_S4_G,
        }
    },
};

static const Bitmap_t bitmaps[] = {
    { .chr = '0', .bitmap = 0b1111110 },
    { .chr = '1', .bitmap = 0b0110000 },
    { .chr = '2', .bitmap = 0b1101101 },
    { .chr = '3', .bitmap = 0b1111001 },
    { .chr = '4', .bitmap = 0b0110011 },
    { .chr = '5', .bitmap = 0b1011011 },
    { .chr = '6', .bitmap = 0b1011111 },
    { .chr = '7', .bitmap = 0b1110000 },
    { .chr = '8', .bitmap = 0b1111111 },
    { .chr = '9', .bitmap = 0b1111011 },
    { .chr = 'a', .bitmap = 0b1110111 },
    { .chr = 'c', .bitmap = 0b0001101 },
    { .chr = 'd', .bitmap = 0b0111101 },
    { .chr = 'p', .bitmap = 0b1100111 },
    { .chr = 's', .bitmap = 0b1011011 },
};

bool Display::init(){
    TM1750_Service::init();
    TM1750_Service::clear();
    TM1750_Service::flush();
    return true;
}

bool Display::clear(){
    return TM1750_Service::clear();
}

bool Display::set_pixel(PIX pix, uint8_t value){
    if ( pix >= (sizeof(pix_infos) / sizeof(PixInfo_t)) ){
        return false;
    }
    return TM1750_Service::set_pixel(pix_infos[pix].seg, pix_infos[pix].com,value);
}

bool Display::set_num_seg(NUM_SEG num_seg, char value){
    NumSegInfo_t num_seg_info;
    if ( num_seg < 0 || num_seg >= (sizeof(num_seg_infos) / sizeof(NumSegInfo_t)) ){
        return false;
    }

    uint8_t bitmap = 0b0000001;
    num_seg_info = num_seg_infos[static_cast<int>(num_seg)];
    for ( int n_bitmap = 0; n_bitmap < (sizeof(bitmaps) / sizeof(Bitmap_t)); n_bitmap++ ){
        if ( bitmaps[n_bitmap].chr == value ) {
            bitmap = bitmaps[n_bitmap].bitmap;
        }
    }

    /* 按照映射表来显示数值. */
    Display::set_pixel(static_cast<Display::PIX>(num_seg_info.pix_index.a), (bitmap & 0b1000000) ? 1 : 0);
    Display::set_pixel(static_cast<Display::PIX>(num_seg_info.pix_index.b), (bitmap & 0b0100000) ? 1 : 0);
    Display::set_pixel(static_cast<Display::PIX>(num_seg_info.pix_index.c), (bitmap & 0b0010000) ? 1 : 0);
    Display::set_pixel(static_cast<Display::PIX>(num_seg_info.pix_index.d), (bitmap & 0b0001000) ? 1 : 0);
    Display::set_pixel(static_cast<Display::PIX>(num_seg_info.pix_index.e), (bitmap & 0b0000100) ? 1 : 0);
    Display::set_pixel(static_cast<Display::PIX>(num_seg_info.pix_index.f), (bitmap & 0b0000010) ? 1 : 0);
    Display::set_pixel(static_cast<Display::PIX>(num_seg_info.pix_index.g), (bitmap & 0b0000001) ? 1 : 0);

    return true;
}

bool Display::flush(){
    return TM1750_Service::flush();
}