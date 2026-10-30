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
    mavlink_start_checksum(msg);
    const char *vector="123456789";
    for(const char *p=vector;*p;++p) mavlink_update_checksum(msg,*p);
    return msg->checksum==0x6f91 ? 0 : 1;
}
