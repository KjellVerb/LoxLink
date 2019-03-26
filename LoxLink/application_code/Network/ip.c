#include "ip.h"
#include "arp.h"
#include "icmp.h"
#include "lan.h"
#include "tcp.h"
#include "udp.h"
#include <string.h>

#define IP_PACKET_TTL (64)

/***
 *  calculate IP checksum
 ***/
uint16_t ip_cksum(uint32_t sum, const void *buf, uint16_t len) {
  const uint8_t *bufByte = buf;
  while (len >= 2) {
    sum += ((uint16_t)*bufByte << 8) | *(bufByte + 1);
    bufByte += 2;
    len -= 2;
  }
  if (len)
    sum += (uint16_t)*bufByte << 8;
  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return ~htons((uint16_t)sum);
}

/***
 *  send IP packet
 *  fields must be set:
 *  - ip.dst
 *  - ip.proto
 *  len is IP packet payload length
 ***/
uint8_t ip_send(eth_frame_t *frame, uint16_t len) {
  ip_packet_t *ip = (ip_packet_t *)(frame->data);

  // set frame.dst
  if (ip->to_addr == gLAN_IPv4_broadcast_address) {
    // use broadcast MAC
    memset(frame->to_addr, 0xff, 6);
  } else {
    // apply route
    uint32_t route_ip;
    if (((ip->to_addr ^ gLAN_IPv4_address) & gLAN_IPv4_subnet_mask) == 0)
      route_ip = ip->to_addr;
    else
      route_ip = gLan_IPv4_gateway;

    // resolve mac address
    uint8_t *mac_addr_to = arp_resolve(route_ip);
    if (!mac_addr_to)
      return 0;
    memcpy(frame->to_addr, mac_addr_to, 6);
  }

  // set frame.type
  frame->type = ETH_TYPE_IP;

  // fill IP header
  len += sizeof(ip_packet_t);
  ip->ver_head_len = 0x45;
  ip->tos = 0;
  ip->total_len = htons(len);
  ip->fragment_id = 0;
  ip->flags_framgent_offset = 0;
  ip->ttl = IP_PACKET_TTL;
  ip->cksum = 0;
  ip->from_addr = gLAN_IPv4_address;
  ip->cksum = ip_cksum(0, ip, sizeof(ip_packet_t));

  eth_send(frame, len);
  return 1;
}

/***
 *  send IP packet back
 *  len is IP packet payload length
 ***/
void ip_reply(eth_frame_t *frame, uint16_t len) {
  ip_packet_t *packet = (ip_packet_t *)(frame->data);

  len += sizeof(ip_packet_t);

  packet->total_len = htons(len);
  packet->fragment_id = 0;
  packet->flags_framgent_offset = 0;
  packet->ttl = IP_PACKET_TTL;
  packet->cksum = 0;
  packet->to_addr = packet->from_addr; // switch the IPv4 addresses for a reply
  packet->from_addr = gLAN_IPv4_address;
  packet->cksum = ip_cksum(0, packet, sizeof(ip_packet_t));

  eth_reply(frame, len);
}

/***
 *  can be called directly after
 *  ip_send/ip_reply with new data
 ***/
void ip_resend(eth_frame_t *frame, uint16_t len) {
  ip_packet_t *ip = (ip_packet_t *)(frame->data);

  len += sizeof(ip_packet_t);
  ip->total_len = htons(len);
  ip->cksum = 0;
  ip->cksum = ip_cksum(0, ip, sizeof(ip_packet_t));
  eth_resend(frame, len);
}

/***
 *  process IP packet
 ***/
void ip_filter(eth_frame_t *frame, uint16_t len) {
  COMPILE_CHECK(sizeof(ip_packet_t) == 20);
  ip_packet_t *packet = (ip_packet_t *)(frame->data);

  uint16_t hcs = packet->cksum;
  packet->cksum = 0;

  if ((packet->ver_head_len == 0x45) &&
      (ip_cksum(0, packet, sizeof(ip_packet_t)) == hcs) &&
      ((packet->to_addr == gLAN_IPv4_address) || (packet->to_addr == gLAN_IPv4_broadcast_address))) {
    len = ntohs(packet->total_len) - sizeof(ip_packet_t);
    switch (packet->protocol) {
#ifdef WITH_ICMP
    case IP_PROTOCOL_ICMP:
      icmp_filter(frame, len);
      break;
#endif

#ifdef WITH_UDP
    case IP_PROTOCOL_UDP:
      udp_filter(frame, len);
      break;
#endif

#ifdef WITH_TCP
    case IP_PROTOCOL_TCP:
      tcp_filter(frame, len);
      break;
#endif
    }
  }
}