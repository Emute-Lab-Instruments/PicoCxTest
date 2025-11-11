#ifndef __STREAMMESSAGING_H__
#define __STREAMMESSAGING_H__

namespace streamMessaging {

  constexpr uint8_t MAGIC_BYTE = 0b10101010;

  enum messageTypes {WAVELEN0, BANK0, BANK1, CTRL,
                  CTRL0, CTRL1, CTRL2, CTRL3, CTRL4, CTRL5, DETUNE, OCTSPREAD,
                  METAMOD3, METAMOD4, METAMOD5, METAMOD6, METAMOD7, METAMOD8};

  struct __attribute__((packed)) msgpacket {
      union {
        float floatValue;
        size_t uintValue;
      } value;
      uint8_t msgType;
      const uint8_t magicByte = MAGIC_BYTE;
      uint16_t checksum;
  };

  static_assert(sizeof(msgpacket) == 8, "msgpacket must be 8 bytes");

  __always_inline void calcCheckSum(msgpacket &msg) {
    uint32_t v = msg.value.uintValue;
    msg.checksum = (uint16_t)(v ^ (v >> 16) ^ msg.msgType);  
  }
  __always_inline void createMessage(msgpacket &msg, const float value, const messageTypes msgType) {
    msg.value.floatValue = value;
    msg.msgType = msgType;
    calcCheckSum(msg);
  };

  __always_inline void createMessage(msgpacket &msg, const size_t value, const messageTypes msgType) {
    msg.value.uintValue = value;
    msg.msgType = msgType;
    calcCheckSum(msg);
  };

  __always_inline bool checksumIsOk(msgpacket *msg) {
    uint32_t v = msg->value.uintValue;
    return msg->checksum == (uint16_t)(v ^ (v >> 16) ^ msg->msgType);  
     
  }

  __always_inline bool magicByteOk(msgpacket *msg) {
    return msg->magicByte == MAGIC_BYTE;
  }


};



#endif