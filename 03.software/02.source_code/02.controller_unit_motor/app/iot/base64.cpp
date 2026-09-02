#include "base64.hpp"
#include "cstdlib"

int base64_encode(char *output, const unsigned char *input, int input_len)
{
    // Base64 编码表
    static const char base64_table[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    int i = 0, j = 0;
    unsigned char a, b, c;
    
    // 每次处理 3 个字节
    while (i < input_len) {
        a = input[i++];
        b = (i < input_len) ? input[i++] : 0;
        c = (i < input_len) ? input[i++] : 0;
        
        // 将 3 个字节拆分为 4 个 6 位索引
        output[j++] = base64_table[a >> 2];
        output[j++] = base64_table[((a & 0x03) << 4) | (b >> 4)];
        output[j++] = base64_table[((b & 0x0F) << 2) | (c >> 6)];
        output[j++] = base64_table[c & 0x3F];
    }
    
    // 处理填充（等号）
    int pad = input_len % 3;
    if (pad == 1) {
        // 最后只剩 1 个字节，需要补 2 个 '='
        output[j - 1] = '=';
        output[j - 2] = '=';
    } else if (pad == 2) {
        // 最后只剩 2 个字节，需要补 1 个 '='
        output[j - 1] = '=';
    }
    
    // 添加字符串结束符
    output[j] = '\0';
    
    return j;  // 返回编码后的长度（不包含 '\0'）
}


int base64_decode(uint8_t *out_buf, const uint8_t *in_b64, size_t b64_len)
{
    static const int8_t b64_table[] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };

    if (out_buf == NULL || in_b64 == NULL) {
        return -1;
    }

    size_t out_idx = 0;
    size_t i = 0;

    while (i < b64_len)
    {
        int8_t v0 = b64_table[in_b64[i++]];
        int8_t v1 = b64_table[in_b64[i++]];
        int8_t v2 = -1;
        int8_t v3 = -1;

        if (i < b64_len) v2 = b64_table[in_b64[i++]];
        if (i < b64_len) v3 = b64_table[in_b64[i++]];

        // 非法字符
        if (v0 == -1 || v1 == -1) {
            return -1;
        }

        out_buf[out_idx++] = (uint8_t)((v0 << 2) | ((v1 & 0x30) >> 4));

        if (v2 != -1 && in_b64[i-2] != '=') {
            out_buf[out_idx++] = (uint8_t)(((v1 & 0x0f) << 4) | ((v2 & 0x3c) >> 2));
        }
        if (v3 != -1 && in_b64[i-1] != '=') {
            out_buf[out_idx++] = (uint8_t)(((v2 & 0x03) << 6) | v3);
        }
    }

    return (int)out_idx;
}