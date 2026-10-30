#include <cstdio>
#include <new>
#include <opensolo/mavlink/solo/mavlink.h>
int main() {
    alignas(8) unsigned char storage[sizeof(mavlink_message_t)+1];
    auto *msg = new (storage+1) mavlink_message_t{};
    mavlink_msg_heartbeat_pack(1,1,msg,2,3,0,0,4);
    unsigned char wire[MAVLINK_MAX_PACKET_LEN];
    unsigned count=mavlink_msg_to_send_buffer(wire,msg);
    for(unsigned i=0;i<count;++i) std::printf("%02x",wire[i]);
    std::puts("");
    const uint16_t expected = msg->checksum;
    mavlink_status_t status = {};
    mavlink_finalize_message_buffer(msg, 1, 1, &status, 9, 9, 50);
    if (msg->checksum != expected) return 2;
    mavlink_start_checksum(msg);
    const char *vector="123456789";
    for(const char *p=vector;*p;++p) mavlink_update_checksum(msg,*p);
    return msg->checksum==0x6f91 ? 0 : 1;
}
