#ifndef BASE64_HPP
#define BASE64_HPP

#include <cstddef>
#include <cstdint>

int base64_encode(char *base64_buffer, const unsigned char *data, int len);
int base64_decode(uint8_t *out_buf, const uint8_t *in_b64, size_t b64_len);

#endif
