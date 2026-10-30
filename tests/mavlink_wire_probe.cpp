#include <stdint.h>
#include <stdio.h>
#include <opensolo/mavlink/solo/mavlink.h>
int main() {
    mavlink_message_t message = {};
    mavlink_status_t status = {};
    int byte;
    unsigned count = 0;
    while ((byte = getchar()) != EOF) {
        if (mavlink_parse_char(MAVLINK_COMM_0, uint8_t(byte), &message, &status)) {
            uint8_t wire[MAVLINK_MAX_PACKET_LEN];
            const uint16_t length = mavlink_msg_to_send_buffer(wire, &message);
            if (fwrite(wire, 1, length, stdout) != length) return 1;
            ++count;
        }
    }
    return ferror(stdin) || count == 0;
}
