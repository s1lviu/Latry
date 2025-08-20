/*
 * Copyright (C) 2025 Silviu YO6SAY
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef REFLECTORPROTOCOL_H
#define REFLECTORPROTOCOL_H

#include <cstdint>
#include <cstring>

namespace Svxlink {

// --- Namespaces for Protocol Constants ---
namespace MsgType {
// Message type identifiers used in Reflector V2
const uint16_t HEARTBEAT           = 1;
const uint16_t PROTO_VER           = 5;
const uint16_t PROTO_VER_DOWNGRADE = 6;
const uint16_t AUTH_CHALLENGE      = 10;
const uint16_t AUTH_RESPONSE       = 11;
const uint16_t AUTH_OK             = 12;
const uint16_t ERROR               = 13;  // Renamed from AUTH_DENIED for accuracy
const uint16_t SERVER_INFO         = 100;
const uint16_t NODE_LIST           = 101;
const uint16_t NODE_JOINED         = 102;
const uint16_t NODE_LEFT           = 103;
const uint16_t TALKER_START        = 104;
const uint16_t TALKER_STOP         = 105;
const uint16_t SELECT_TG           = 106;
const uint16_t TG_MONITOR          = 107;
const uint16_t REQUEST_QSY         = 109;
const uint16_t STATE_EVENT         = 110;
const uint16_t NODE_INFO           = 111;
const uint16_t SIGNAL_STRENGTH     = 112;
const uint16_t TX_STATUS           = 113;
}

namespace UdpMsgType {
const uint16_t UDP_HEARTBEAT           = 1;    // Fixed: SVXLink uses 1, not 100
const uint16_t UDP_AUDIO               = 101;
const uint16_t UDP_FLUSH_SAMPLES       = 102;
const uint16_t UDP_ALL_SAMPLES_FLUSHED = 103;
const uint16_t UDP_SIGNAL_STRENGTH     = 104;
}

namespace Protocol {
const uint16_t MAJOR_VER = 2;
const uint16_t MINOR_VER = 0;
const int CHALLENGE_LEN = 20;
const int DIGEST_LEN = 20;
const int CALLSIGN_LEN = 20;
}

#pragma pack(push, 1)

// Generic Message Header
// All protocol messages start with a 16-bit type field. There is no
// size field inside the message. The size is provided by the 32-bit
// length prefix of the TCP frame.
struct MsgHeader {
    uint16_t type;
};

// Type 0: Protocol Version
struct MsgProtoVer : public MsgHeader {
    uint16_t major_ver;
    uint16_t minor_ver;
};

// Type 1: Heartbeat
struct MsgHeartbeat : public MsgHeader {};

// Type 2: Authentication Challenge (Server -> Client)
struct MsgAuthChallenge : public MsgHeader {
    uint8_t challenge[Protocol::CHALLENGE_LEN];
};

// Type 3: Authentication Response (Client -> Server)
struct MsgAuthResponse : public MsgHeader {
    uint8_t digest[Protocol::DIGEST_LEN];
    char    callsign[Protocol::CALLSIGN_LEN];
};

// Type 4: Server Info (Server -> Client)
struct MsgServerInfo : public MsgHeader {
    uint16_t reserved;
    uint16_t clientId;
};

// Type 5: Node Info (Client -> Server)
struct MsgNodeInfo : public MsgHeader {
    uint16_t json_len;
    char     json_data[];
};

// Type 6: Protocol Version Downgrade (Server -> Client)
struct MsgProtoVerDowngrade : public MsgHeader {
    uint16_t major_ver;
    uint16_t minor_ver;
};

// Type 12: Authentication OK (Server -> Client)
struct MsgAuthOk : public MsgHeader {};

// Type 13: Error (Server -> Client)
struct MsgError : public MsgHeader {
    uint16_t msg_len;
    char     msg_data[];
};

// Type 101: Node List (Server -> Client)
struct MsgNodeList : public MsgHeader {
    uint16_t node_count;
    // Followed by array of node info structures
};

// Type 102: Node Joined (Server -> Client)
struct MsgNodeJoined : public MsgHeader {
    char callsign[Protocol::CALLSIGN_LEN];
};

// Type 103: Node Left (Server -> Client)
struct MsgNodeLeft : public MsgHeader {
    char callsign[Protocol::CALLSIGN_LEN];
};

// Type 106: Select Talkgroup
struct MsgSelectTG : public MsgHeader {
    uint32_t talkgroup;
};

// Type 107: Talk Group Monitor (Client -> Server)
struct MsgTgMonitor : public MsgHeader {
    uint16_t tg_count;
    uint32_t talkgroups[];
};

// Type 109: Request QSY (Client -> Server)
struct MsgRequestQsy : public MsgHeader {
    uint32_t talkgroup;
};

// Type 110: State Event (Client -> Server)
struct MsgStateEvent : public MsgHeader {
    uint16_t src_len;
    uint16_t name_len;
    uint16_t msg_len;
    // Followed by src, name, message strings
};

// Type 112: Signal Strength Values (Bidirectional)
struct MsgSignalStrength : public MsgHeader {
    float    rx_signal_strength;
    float    rx_sql_open;
    char     callsign[Protocol::CALLSIGN_LEN];
};

// Type 113: TX Status (Client -> Server)
struct MsgTxStatus : public MsgHeader {
    uint8_t  tx_state;      // 0=off, 1=on
    char     callsign[Protocol::CALLSIGN_LEN];
};

// --- UDP Structs ---
struct UdpMsgHeader {
    uint16_t type;
    uint16_t clientId;
    uint16_t sequenceNum;
};

struct MsgUdpAudio : public UdpMsgHeader {
    uint16_t audioLen;
    uint8_t  audioData[];
};

struct MsgUdpHeartbeat : public UdpMsgHeader {};
struct MsgUdpFlushSamples : public UdpMsgHeader {};
struct MsgUdpAllSamplesFlushed : public UdpMsgHeader {};
struct MsgUdpSignalStrength : public UdpMsgHeader {
    float    rx_signal_strength;
    float    rx_sql_open;
    char     callsign[Protocol::CALLSIGN_LEN];
};

#pragma pack(pop)

} // namespace Svxlink

#endif // REFLECTORPROTOCOL_H
