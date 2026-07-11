/*
 * MinNT - ndis/ndis.h
 * NDIS library header — minimal implementation for WiFi miniport.
 *
 * Provides NdisM* functions that WiFi miniport drivers call.
 * Real NDIS 6.0 is complex; this is the subset needed for rtw88.
 */

#ifndef _NDIS_H_
#define _NDIS_H_

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/io.h>
#include <nt/rtl.h>
#include <nt/dispatcher.h>

/* ---- NDIS version -------------------------------------------------- */

#define NDIS_MAKE_VERSION(major, minor) (((major) << 16) | (minor))
#define NDIS_MINIPORT_MAJOR_VERSION     6
#define NDIS_MINIPORT_MINOR_VERSION     0
#define NDIS_VERSION                    NDIS_MAKE_VERSION(NDIS_MINIPORT_MAJOR_VERSION, NDIS_MINIPORT_MINOR_VERSION)

/* ---- NDIS status codes --------------------------------------------- */

typedef ULONG NDIS_STATUS, *PNDIS_STATUS;

#define NDIS_STATUS_SUCCESS          ((NDIS_STATUS)0x00000000)
#define NDIS_STATUS_PENDING          ((NDIS_STATUS)0x00000001)
#define NDIS_STATUS_NOT_RECOGNIZED   ((NDIS_STATUS)0x00010000)
#define NDIS_STATUS_NOT_RESETTED     ((NDIS_STATUS)0x00010001)
#define NDIS_STATUS_NOT_INDICATING   ((NDIS_STATUS)0x00010002)
#define NDIS_STATUS_FAILURE          ((NDIS_STATUS)0xC0000001)
#define NDIS_STATUS_RESOURCES        ((NDIS_STATUS)0xC000009A)
#define NDIS_STATUS_INVALID_PARAMETER ((NDIS_STATUS)0xC000000D)
#define NDIS_STATUS_INVALID_DATA      ((NDIS_STATUS)0xC0000010)
#define NDIS_STATUS_ADAPTER_NOT_FOUND ((NDIS_STATUS)0xC0000200)
#define NDIS_STATUS_NOT_IMPLEMENTED  ((NDIS_STATUS)0xC00000BB)
#define NDIS_STATUS_BUFFER_TOO_SHORT ((NDIS_STATUS)0xC0000207)

/* ---- OID type ----------------------------------------------------- */

typedef ULONG NDIS_OID;

/* ---- NDIS OID definitions ----------------------------------------- */

/* OID_GEN OIDs */
#define OID_GEN_SUPPORTED_LIST                    0x00010101
#define OID_GEN_HARDWARE_STATUS                   0x00010102
#define OID_GEN_MEDIA_SUPPORTED                   0x00010103
#define OID_GEN_MEDIA_IN_USE                      0x00010104
#define OID_GEN_MAXIMUM_LOOKAHEAD                 0x00010105
#define OID_GEN_MAXIMUM_FRAME_SIZE                0x00010106
#define OID_GEN_LINK_SPEED                        0x00010107
#define OID_GEN_NETWORK_LINK_SPEED                0x00010107
#define OID_GEN_CURRENT_PACKET_FILTER             0x00010004
#define OID_GEN_TRANSMIT_BUFFER_SPACE             0x00010108
#define OID_GEN_RECEIVE_BUFFER_SPACE              0x00010109
#define OID_GEN_TRANSMIT_BLOCK_SIZE               0x0001010A
#define OID_GEN_RECEIVE_BLOCK_SIZE                0x0001010B
#define OID_GEN_VENDOR_ID                       0x0001010C
#define OID_GEN_VENDOR_DESCRIPTION              0x0001010D
#define OID_GEN_VENDOR_DRIVER_VERSION           0x0001010E
#define OID_GEN_CURRENT_MEDIA_SUPPORTED         0x0001010F
#define OID_GEN_CURRENT_MEDIA_IN_USE            0x00010110
#define OID_GEN_MEDIA_CONNECT_STATUS            0x00010111
#define OID_GEN_MAXIMUM_SEND_PACKETS            0x00010112
#define OID_GEN_SUPPORTED_MAC_LIST              0x00010113
#define OID_GEN_SUPPORTED_MAC_ADDRESSES         0x00010114
#define OID_GEN_VENDOR_DRIVER_VERSION_EX        0x00010115
#define OID_GEN_XMIT_CONNECTIVITY               0x00010116
#define OID_GEN_SUPPORTED_SPEEDS                0x00010117
#define OID_GEN_CURRENT_LOOKAHEAD               0x00010118
#define OID_GEN_DRIVER_VERSION                  0x00010119
#define OID_GEN_MAXIMUM_TOTAL_SIZE              0x0001011A
#define OID_GEN_PROTOCOL_OPTIONS                0x0001011B
#define OID_GEN_MAC_OPTIONS                     0x0001011C
#define OID_GEN_MEDIA_CAPABILITIES              0x0001011D
#define OID_GEN_PHYSICAL_MEDIA_TYPE             0x0001011E
#define OID_GEN_MAC_STATE                       0x0001011F
#define OID_GEN_ALTERNATIVE_MAC_LIST            0x00010120
#define OID_GEN_FLT_FILTER_LIST                 0x00010121
#define OID_GEN_LINK_PARAMETERS                 0x00010122
#define OID_GEN_INTERRUPT_COALESCENCE           0x00010123
#define OID_GEN_MAC_CAPABILITIES                0x00010124
#define OID_GEN_RANDOM_MAC_ADDRESS              0x00010125
#define OID_GEN_READMAX_TOTAL_SIZE              0x00010126
#define OID_GEN_MEDIA_ENUMERATION               0x00010127
#define OID_GEN_ENUMERATE_PORTS                 0x00010128
#define OID_GEN_PERMANENT_MAC_PARAM             0x00010129

/* OID_GEN statistics */
#define OID_GEN_XMIT_OK                         0x00020001
#define OID_GEN_RCV_OK                          0x00020002
#define OID_GEN_XMIT_ERROR                      0x00020003
#define OID_GEN_RCV_ERROR                       0x00020004
#define OID_GEN_RCV_NO_BUFFER                   0x00020005
#define OID_GEN_DIRECTED_BYTES_XMIT             0x00020006
#define OID_GEN_DIRECTED_FRAMES_XMIT            0x00020007
#define OID_GEN_MULTICAST_BYTES_XMIT            0x00020008
#define OID_GEN_MULTICAST_FRAMES_XMIT           0x00020009
#define OID_GEN_BROADCAST_BYTES_XMIT            0x0002000A
#define OID_GEN_BROADCAST_FRAMES_XMIT           0x0002000B
#define OID_GEN_DIRECTED_BYTES_RCV              0x0002000C
#define OID_GEN_DIRECTED_FRAMES_RCV             0x0002000D
#define OID_GEN_MULTICAST_BYTES_RCV              0x0002000E
#define OID_GEN_MULTICAST_FRAMES_RCV            0x0002000F
#define OID_GEN_BROADCAST_BYTES_RCV             0x00020010
#define OID_GEN_BROADCAST_FRAMES_RCV            0x00020011
#define OID_GEN_RCV_CRC_ERROR                   0x00020012
#define OID_GEN_TRANSMIT_QUEUE_LENGTH           0x00020013
#define OID_GEN_STATISTICS                      0x0002001E
#define OID_GEN_ENCAP_SUPPORTED                 0x0001000E
#define OID_GEN_MEDIA_TYPE                      0x00010001
#define OID_GEN_GET_TIME_CAPS                   0x00020014
#define OID_GEN_SET_TIME_CAPS                   0x00020015
#define OID_GEN_USE_RUNS                        0x00020016
#define OID_GEN_RESET_COUNTS                    0x00020017
#define OID_GEN_MEDIA_SEPARATION_OK             0x00020018
#define OID_GEN_GET_TCP_CHECKSUM_CAPS           0x00020019
#define OID_GEN_SET_TCP_CHECKSUM_CAPS             0x0002001A
#define OID_GEN_XMIT_TCP_CHECKSUM             0x0002001B
#define OID_GEN_RCV_TCP_CHECKSUM              0x0002001C
#define OID_GEN_RCV_OVERRUN                     0x0002001D
#define OID_GEN_XMIT_ONECAST                  0x0002001E
#define OID_GEN_RCV_ONECAST                   0x0002001F
#define OID_GEN_XMIT_MULTICAST                0x00020020
#define OID_GEN_RCV_MULTICAST                 0x00020021
#define OID_GEN_RCV_MISALIGNED_BITS             0x00020022
#define OID_GEN_HOST_BROADCAST_MAC_RECEIVED     0x00020023
#define OID_GEN_HOST_MULTICAST_MAC_RECEIVED     0x00020024
#define OID_GEN_HOST_DIR_MAC_RECEIVED           0x00020025
#define OID_GEN_HOST_BROADCAST_MAC_TRANSMITTED  0x00020026
#define OID_GEN_HOST_MULTICAST_MAC_TRANSMITTED  0x00020027
#define OID_GEN_HOST_DIR_MAC_TRANSMITTED        0x00020028
#define OID_GEN_XMIT_BYTES                      0x00020029
#define OID_GEN_RCV_BYTES                       0x0002002A
#define OID_GEN_RCV_ERROR_CHECKING              0x0002002C
#define OID_GEN_STATISTICS_VERSION              0x0002002D
#define OID_GEN_XMIT_UNKNOWN_PROTOCOL           0x0002002E
#define OID_GEN_RCV_INTERRUPT                   0x0002002F
#define OID_GEN_RCV_INTERRUPT_NO_PACKETS        0x00020030

/* OID_802_11 OIDs */
#define OID_802_11_BSSID                        0x00010101
#define OID_802_11_BSSID_LIST                   0x00010102
#define OID_802_11_SSID                         0x00010103
#define OID_802_11_DISASSOCIATE                 0x00010104
#define OID_802_11_BSSID_LIST_SCAN              0x00010105
#define OID_802_11_AUTOMATIC_MODE               0x00010106
#define OID_802_11_NETWORK_TYPE_LIST            0x00010107
#define OID_802_11_NETWORK_TYPE                 0x00010108
#define OID_802_11_PROTECTION_MANAGER           0x00010109
#define OID_802_11_JOIN_LIST                    0x0001010A
#define OID_802_11_ADD_NETWORK                    0x0001010B
#define OID_802_11_REMOVE_NETWORK               0x0001010C
#define OID_802_11_ADD_NETWORK_TEMPLATE         0x0001010D
#define OID_802_11_REMOVE_NETWORK_TEMPLATE      0x0001010E
#define OID_802_11_CLEAR_NETWORK Templates      0x0001010F
#define OID_802_11_NETWORK_LIST                 0x00010110
#define OID_802_11_NETWORK_LIST_OPTIONS         0x00010111
#define OID_802_11_SCAN_OPTIONS                 0x00010112
#define OID_802_11_DISASSOCIATE_OPTIONS         0x00010113
#define OID_802_11_MAC_ADDRESS                  0x00010114
#define OID_802_11_NETWORK_LIST_IDS             0x00010115
#define OID_802_11_CURRENT_OPERATING_MODE       0x00010116
#define OID_802_11_CAPABILITY                   0x00010117
#define OID_802_11_PMKID                      0x00010118
#define OID_802_11_CURRENT_TX_POWER_LEVEL       0x00010119
#define OID_802_11_DSSS_MODE                    0x0001011A
#define OID_802_11_THROTTLE_PARAMETERS          0x0001011B
#define OID_802_11_ANTENNA                      0x0001011C
#define OID_802_11_BSSTYPE                      0x0001011D
#define OID_802_11_TX_RETRIES                   0x0001011E
#define OID_802_11_RSSI                         0x0001011F
#define OID_802_11_ENCRYPTION_STATUS            0x00010120
#define OID_802_11_CTSPROTECTION                0x00010121
#define OID_802_11_DECRYPT_STATUS               0x00010122
#define OID_802_11_CURRENT_AUTH_ALG             0x00010123
#define OID_802_11_REMOVE_KEY                   0x00010124
#define OID_802_11_ADD_KEY                      0x00010125
#define OID_802_11_REQUEST_KEY_ID               0x00010126
#define OID_802_11_PRIVACY_RESTRICTED           0x00010127
#define OID_802_11_PRIVACY_OPTIONS              0x00010128
#define OID_802_11_PRIVACY_STATUS               0x00010129
#define OID_802_11_P2P_GO_INTENT                0x0001012A
#define OID_802_11_P2P_OPPORTUNISTIC_PMKID      0x0001012B
#define OID_802_11_P2P_PROVISION_DISCOVERY      0x0001012C
#define OID_802_11_P2P_HOSTED_NETWORK_ACCEPT_CONNECTION   0x0001012D
#define OID_802_11_P2P_HOSTED_NETWORK_CANCEL_CONNECTION   0x0001012E
#define OID_802_11_P2P_HOSTED_NETWORK_ALLOW_CURRENT_AUTH_ONLY 0x0001012F
#define OID_802_11_RANDMAC_REASSOCIATE_CAPABLE    0x00010130
#define OID_802_11_RANDMAC_REASSOCIATION        0x00010131
#define OID_802_11_LIGHTMODE                     0x00010132
#define OID_802_11_TX_RATE                      0x00010133
#define OID_802_11_LINK_STATE                   0x00010134
#define OID_802_11_LINK_STATE_OPTIONS           0x00010135
#define OID_802_11_LINK_STATE_SCAN_AGGREGATION  0x00010136
#define OID_802_11_P2P_NOA                      0x00010137
#define OID_802_11_P2P_AP_ON_GO_CHANNEL         0x00010138
#define OID_802_11_P2P_HOSTED_NETWORK_ALLOW_AP    0x00010139
#define OID_802_11_P2P_HOSTED_NETWORK         0x0001013A
#define OID_802_11_P2P_PROV_DISC_TARGET         0x0001013B
#define OID_802_11_P2P_PROV_DISC_TARGET_REM     0x0001013C
#define OID_802_11_P2P_LISTEN_TXOP              0x0001013D
#define OID_802_11_ANTENNA_SELECTED             0x0001013E
#define OID_802_11_QOS_CAPABILITY               0x0001013F
#define OID_802_11_DFS_COUNTRY_CODE             0x00010140
#define OID_802_11_SCAN_RAW_FRAME               0x00010141
#define OID_802_11_DFS_CHANNEL_INFO             0x00010142
#define OID_802_11_OBSS_SCAN                    0x00010143
#define OID_802_11_OBSS_SCAN_PARAMETERS         0x00010144
#define OID_802_11_OBSS_SCAN_RESULTS            0x00010145
#define OID_802_11_ADAPTATION_MODE              0x00010146
#define OID_802_11_TDLS_SCAN                    0x00010147
#define OID_802_11_TDLS_DEVICE_INFO             0x00010148
#define OID_802_11_TDLS_OPER_MODE               0x00010149
#define OID_802_11_TDLS_OPER_MODE_CAPABILITIES  0x0001014A
#define OID_802_11_TDLS_SETTINGS                  0x0001014B
#define OID_802_11_SAE_PWE_HAPD                 0x0001014C
#define OID_802_11_SAE_PWE_MGD                  0x0001014D
#define OID_802_11_FILS_DISCOVERY               0x0001014E
#define OID_802_11_WNM_NOTIFICATION             0x0001014F
#define OID_802_11_WNM_NOTIFICATION_OPTIONS     0x00010150
#define OID_802_11_BSS_LISTEN_MODE              0x00010151
#define OID_802_11_BAND_OPTIONS                 0x00010152
#define OID_802_11_BAND_OPTIONS_EX              0x00010153
#define OID_802_11_BAND_TX_POWER_EX             0x00010154
#define OID_802_11_BAND_EXT_TYPE                0x00010155
#define OID_802_11_CHANNEL_LOAD                 0x00010156
#define OID_802_11_CONNECTION_QUALITY           0x00010157
#define OID_802_11_EAPD                         0x00010158
#define OID_802_11_RSSILIST                     0x00010159
#define OID_802_11_HE_OBSS_PD                   0x0001015A
#define OID_802_11_HE_OBSS_PD_PARAMETERS        0x0001015B
#define OID_802_11_HE_OBSS_PD_BSS_COLOR         0x0001015C
#define OID_802_11_RRM                          0x0001015D
#define OID_802_11_MBO_GROUP_TYPE_DATA          0x0001015E
#define OID_802_11_MBO_ASSOC_DISALLOW           0x0001015F
#define OID_802_11_BCN_RSSI_THRESHOLD           0x00010160
#define OID_802_11_MBO_DATA                   0x00010161
#define OID_802_11_NON_PREF_CHAN_REPORT         0x00010162
#define OID_802_11_CHANNEL_CENTER_FREQ_RANGE    0x00010163
#define OID_802_11_BAND_TRANSITION              0x00010164
#define OID_802_11_BAND_TRANSITION_OPTIONS      0x00010165
#define OID_802_11_FTM_RESPONDER              0x00010166
#define OID_802_11_FTM_RESPONDER_CAPABILITIES 0x00010167
#define OID_802_11_RIS                         0x00010168
#define OID_802_11_RIS_CAPABILITIES             0x00010169
#define OID_802_11_RIS_CONFIG                     0x0001016A
#define OID_802_11_RIS_RESULTS                    0x0001016B
#define OID_802_11_HE_BSS_COLOR                 0x0001016C
#define OID_802_11_HE_BSS_COLOR_OPTIONS         0x0001016D
#define OID_802_11_EHT_OPERATION                0x0001016E
#define OID_802_11_EHT_OPERATION_OPTIONS        0x0001016F
#define OID_802_11_MLO_LINK_CONFIG              0x00010170
#define OID_802_11_MLO_LINK_CONFIG_OPTIONS      0x00010171
#define OID_802_11_MLO_LINK_STATE               0x00010172
#define OID_802_11_MLO_LINK_INFO                0x00010173
#define OID_802_11_MLO_LINK_RECOVERY            0x00010174
#define OID_802_11_MLO_LINK_RECOVERY_OPTIONS    0x00010175
#define OID_802_11_MLO_LINK_RECOVERY_RESULTS    0x00010176
#define OID_802_11_POWER_MODE                   0x00010177
#define OID_802_11_POWER_MODE_OPTIONS           0x00010178
#define OID_802_11_MLO_CAPABILITES              0x00010179
#define OID_802_11_MLO_ROAMING_INFO             0x0001017A
#define OID_802_11_MLO_ROAMING_OPTIONS          0x0001017B
#define OID_802_11_MLO_ROAMING_RESULTS          0x0001017C
#define OID_802_11_MLO_PRIMARY_LINK             0x0001017D
#define OID_802_11_MLO_AP_LIST                  0x0001017E
#define OID_802_11_MLO_LINK_ID                  0x0001017F
#define OID_802_11_MLO_LINK_ID_LIST             0x00010180
#define OID_802_11_MLO_LINK_ID_LIST_SCAN        0x00010181
#define OID_802_11_MLO_LINK_ID_RECOVERY         0x00010182
#define OID_802_11_MLO_LINK_ID_RECOVERY_OPTIONS 0x00010183
#define OID_802_11_MLO_LINK_ID_RECOVERY_RESULTS 0x00010184
#define OID_802_11_MLO_LINK_ID_RECOVERY_STATUS  0x00010185
#define OID_802_11_MLO_LINK_ID_RECOVERY_COMPLETE 0x00010186

/* OID_802 OIDs */
#define OID_802_STATS_VERSION                   0x01010001
#define OID_802_STATS_SUPPORTED_LIST            0x01010002
#define OID_802_STATS_TRANSMIT_OK               0x01010003
#define OID_802_STATS_RECEIVE_OK                0x01010004
#define OID_802_STATS_TRANSMIT_ERROR            0x01010005
#define OID_802_STATS_RECEIVE_ERROR             0x01010006
#define OID_802_STATS_RECEIVE_NO_BUFFER         0x01010007
#define OID_802_STATS_UNICAST_XMIT_OK           0x01010008
#define OID_802_STATS_MULTICAST_XMIT_OK         0x01010009
#define OID_802_STATS_BROADCAST_XMIT_OK         0x0101000A
#define OID_802_STATS_UNICAST_RCV_OK            0x0101000B
#define OID_802_STATS_MULTICAST_RCV_OK          0x0101000C
#define OID_802_STATS_BROADCAST_RCV_OK          0x0101000D
#define OID_802_STATS_TRANSMIT_BYTES_OK         0x0101000E
#define OID_802_STATS_RECEIVE_BYTES_OK          0x0101000F
#define OID_802_STATS_DEFERRED_TRANSMITS        0x01010010
#define OID_802_STATS_SINGLE_COLLISION          0x01010011
#define OID_802_STATS_MULTIPLE_COLLISION        0x01010012
#define OID_802_STATS_EXCESSIVE_COLLISIONS      0x01010013
#define OID_802_STATS_BEACON_LOSS               0x01010014
#define OID_802_STATS_TO_REASSEMBLE_ERROR       0x01010015
#define OID_802_STATS_MAC_RETRANSMIT            0x01010016
#define OID_802_STATS_MAC_DUPLICATE             0x01010017

/* NDIS hardware states */
#define NDIS_HARDWARE_STATUS_READY                      0x00000000
#define NDIS_HARDWARE_STATUS_DISABLING_HARDWARE         0x00000001
#define NDIS_HARDWARE_STATUS_IS_DISABLED                0x00000002

/* NDIS media connect states */
#define NDIS_MEDIA_STATE_CONNECTED                      0x00000000
#define NDIS_MEDIA_STATE_DISCONNECTED                   0x00000001

typedef ULONG NDIS_MEDIA_STATE;

/* NDIS packet filter bits */
#define NDIS_PACKET_TYPE_DIRECTED                       0x00000001
#define NDIS_PACKET_TYPE_MULTICAST                    0x00000002
#define NDIS_PACKET_TYPE_ALL_MULTICAST                  0x00000004
#define NDIS_PACKET_TYPE_BROADCAST                    0x00000008
#define NDIS_PACKET_TYPE_SOURCE_ROUTING                 0x00000010
#define NDIS_PACKET_TYPE_PROMISCUOUS                    0x00000020
#define NDIS_PACKET_TYPE_ALL_LOCAL                      0x00000040
#define NDIS_PACKET_TYPE_ALL_FUNCTIONAL                 0x00000080
#define NDIS_PACKET_TYPE_FUNCTIONAL                     0x00000100
#define NDIS_PACKET_TYPE_MAC_FRAME                      0x00000200

/* NDIS media types */
#define NDIS_MEDIUM_802_3                               0x00000000
#define NDIS_MEDIUM_802_5                               0x00000001
#define NDIS_MEDIUM_IEEE80211                           0x00000012

/* NDIS physical media types */
#define NDIS_PHYSICAL_MEDIUM_EMPTY                      0x00000000
#define NDIS_PHYSICAL_MEDIUM_802_3                      0x00000001
#define NDIS_PHYSICAL_MEDIUM_802_5                      0x00000002
#define NDIS_PHYSICAL_MEDIUM_IEEE80211                  0x00000003

/* NDIS MAC options */
#define NDIS_MAC_OPTIONS_CONNECTABLE                    0x00000001
#define NDIS_MAC_OPTIONS_FULL_DUPLEX                    0x00000002
#define NDIS_MAC_OPTIONS_EMAC_OPTIONS                   0x00000004

/* ---- NDIS structures for OIDs --------------------------------------- */

typedef struct _NDIS_OID_SUPPORTED_LIST {
    NDIS_OID Oid;
    ULONG OidLength;
} NDIS_OID_SUPPORTED_LIST, *PNDIS_OID_SUPPORTED_LIST;

typedef struct _NDIS_HARDWARE_STATUS {
    NDIS_STATUS HardwareStatus;
    NDIS_STATUS HardwareStatusEx;
} NDIS_HARDWARE_STATUS, *PNDIS_HARDWARE_STATUS;

typedef struct _NDIS_MEDIA_CONNECT_STATUS {
    ULONG ConnectionState;
    ULONG Flags;
} NDIS_MEDIA_CONNECT_STATUS, *PNDIS_MEDIA_CONNECT_STATUS;

typedef struct _NDIS_802_11_SSID {
    ULONG SsidLength;
    UCHAR SSID[32];
} NDIS_802_11_SSID, *PNDIS_802_11_SSID;

typedef struct _NDIS_802_11_BSSID {
    ULONG BssidLength;
    UCHAR BSSID[32];
} NDIS_802_11_BSSID, *PNDIS_802_11_BSSID;

typedef struct _NDIS_802_11_CONFIGURATION {
    ULONG Length;
    ULONG Dot11BssIdLength;
    UCHAR Dot11BSSID[32];
    ULONG Dot11BssIdListLength;
    ULONG BeaconPeriod;
    ULONG ATIMWindow;
    ULONG DSConfig;
    ULONG BHConfig;
    ULONG ShortSlotTime;
    BOOLEAN ShortPreamble;
    BOOLEAN BasicRates;
    ULONG BasicRateLength;
    ULONG HiddenDot11BSSIDListLength;
    UCHAR HiddenDot11BSSIDList[1];
} NDIS_802_11_CONFIGURATION, *PNDIS_802_11_CONFIGURATION;

typedef struct _NDIS_802_11_BSSID_LIST {
    ULONG NumberOfBSSIDs;
    NDIS_802_11_BSSID BSSID[1];
} NDIS_802_11_BSSID_LIST, *PNDIS_802_11_BSSID_LIST;

typedef struct _NDIS_802_11_WEP_STATUS {
    ULONG KeyIndex;
    ULONG Length;
    UCHAR KeyData[1];
} NDIS_802_11_WEP_STATUS, *PNDIS_802_11_WEP_STATUS;

typedef struct _NDIS_802_11_KEY {
    ULONG Length;
    ULONG KeyIndex;
    ULONG KeyType;
    ULONG KeyMaterialLength;
    UCHAR KeyMaterial[1];
} NDIS_802_11_KEY, *PNDIS_802_11_KEY;

typedef struct _NDIS_802_11_AUTH_ALGORITHM {
    ULONG AuthAlgorithm;
    ULONG AuthAlgorithmLength;
    UCHAR AuthAlgorithmInfo[1];
} NDIS_802_11_AUTH_ALGORITHM, *PNDIS_802_11_AUTH_ALGORITHM;

typedef struct _NDIS_802_11_WEP_VERSION {
    USHORT WepMajorVersion;
    USHORT WepMinorVersion;
    ULONG WepKeyLength;
} NDIS_802_11_WEP_VERSION, *PNDIS_802_11_WEP_VERSION;

typedef struct _NDIS_802_11_VPD_INFO {
    ULONG Length;
    UCHAR VpdInfo[1];
} NDIS_802_11_VPD_INFO, *PNDIS_802_11_VPD_INFO;

typedef struct _NDIS_802_11_ENC_ALGORITHM {
    ULONG EncAlgorithm;
    ULONG EncAlgorithmLength;
    UCHAR EncAlgorithmInfo[1];
} NDIS_802_11_ENC_ALGORITHM, *PNDIS_802_11_ENC_ALGORITHM;

typedef struct _NDIS_802_11_RSSI {
    LONG Rssi;
} NDIS_802_11_RSSI, *PNDIS_802_11_RSSI;

typedef struct _NDIS_STATISTICS_INFO {
    ULONG OutboundKBytes;
    ULONG InboundKBytes;
    ULONG OutboundunicastPackets;
    ULONG OutboundMulticastPackets;
    ULONG OutboundBroadcastPackets;
    ULONG InboundunicastPackets;
    ULONG InboundMulticastPackets;
    ULONG InboundBroadcastPackets;
    ULONG OutboundDiscarded;
    ULONG InboundDiscarded;
    ULONG OutboundErrors;
    ULONG InboundErrors;
    ULONG OutboundBadProtocol;
    ULONG InboundBadProtocol;
} NDIS_STATISTICS_INFO, *PNDIS_STATISTICS_INFO;

/* ---- NDIS handle types ---------------------------------------------- */

typedef PVOID NDIS_HANDLE;
typedef PVOID PNDIS_HANDLE;
typedef PVOID NDIS_MINIPORT_ADAPTER_CONTEXT;
typedef PVOID NDIS_PACKET;
typedef PVOID PNDIS_PACKET;
typedef PVOID NDIS_PROTOCOL_CHARACTERISTICS;
typedef PVOID NDIS_MINIPORT_INTERRUPT;
typedef PVOID PNDIS_MINIPORT_INTERRUPT;
typedef PVOID NDIS_MINIPORT_TIMER;
typedef PVOID PNDIS_MINIPORT_TIMER;
typedef PVOID NDIS_INTERFACE_TYPE;
typedef PVOID PNDIS_DMA_DESCRIPTION;
typedef PHYSICAL_ADDRESS NDIS_PHYSICAL_ADDRESS;

/* ---- DMA structures ------------------------------------------------ */

typedef struct _NDIS_DMA_BUFFER {
    PVOID               VirtualAddress;
    NDIS_PHYSICAL_ADDRESS PhysicalAddress;
    ULONG               Length;
    ULONG               CurrentLength;
    ULONG               Pages;
    BOOLEAN             Mapped;
    BOOLEAN             Direction;
} NDIS_DMA_BUFFER, *PNDIS_DMA_BUFFER;

typedef struct _NDIS_DMA_CHANNEL {
    BOOLEAN             Initialized;
    ULONG               ChannelIndex;
    BOOLEAN             Available;
    NDIS_HANDLE         AdapterHandle;
    PNDIS_DMA_DESCRIPTION DmaDescription;
    ULONG               MaximumLength;
    ULONG               CurrentLength;
    BOOLEAN             BusMaster;
    NDIS_INTERFACE_TYPE InterfaceType;
    PNDIS_DMA_BUFFER    Buffer;
} NDIS_DMA_CHANNEL, *PNDIS_DMA_CHANNEL;
typedef ULONG NDIS_ERROR_CODE;

/* ---- NDIS interface types ------------------------------------------ */

#define NDIS_INTERFACE_TYPE_UNDEFINED  0
#define NDIS_INTERFACE_IS_A            1
#define NDIS_INTERFACE_EISA            2
#define NDIS_INTERFACE_HSC             3
#define NDIS_INTERFACE_PCI             5
#define NDIS_INTERFACE_PCIX            6
#define NDIS_INTERFACE_UNKNOWN         7

/* ---- NDIS interrupt mode ------------------------------------------ */

typedef enum _NDIS_INTERRUPT_MODE {
    NdisInterruptLevelSensitive = 0,
    NdisInterruptLatched = 1
} NDIS_INTERRUPT_MODE;

/* ---- Timer callback ------------------------------------------------ */

typedef VOID (NTAPI *PNDIS_TIMER_FUNCTION)(PVOID SystemSpecific1,
                                           PVOID FunctionContext,
                                           PVOID WorkItemContext,
                                           PVOID TimerQueueContext);

/* ---- NDIS_MINIPORT_CHARACTERISTICS (simplified) -------------------- */

typedef struct _NDIS_MINIPORT_CHARACTERISTICS {
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    USHORT Filler;
    PVOID Reserved[11];
    PVOID MiniportSendPacketsHandler;
    PVOID HaltHandler;
    PVOID InitHandler;
    PVOID PauseHandler;
    PVOID QueryInformationHandler;
    PVOID ResetHandler;
    PVOID SendHandler;
    PVOID SetInformationHandler;
    PVOID CheckForHangHandler;
    PVOID ISRHandler;
    PVOID ReturnPacketHandler;
    PVOID SendInterruptHandler;
    PVOID ReceiveInterruptHandler;
    PVOID SendCompleteHandler;
    PVOID ReceiveHandler;
    PVOID ReceiveCompleteHandler;
    PVOID StatusHandler;
    PVOID StatusCompleteHandler;
    PVOID AddressChangeHandler;
    PVOID PnPEventNotifyHandler;
    PVOID AdapterShutdownHandler;
} NDIS_MINIPORT_CHARACTERISTICS, *PNDIS_MINIPORT_CHARACTERISTICS;

/* ---- NDIS wrapper context ------------------------------------------ */

typedef struct _NDIS_WRAPPER_CONTEXT {
    BOOLEAN             Initialized;
    PDRIVER_OBJECT      DriverObject;
    PVOID               SystemSpecific1;
    PVOID               SystemSpecific2;
    PVOID               SystemSpecific3;
    NDIS_MINIPORT_CHARACTERISTICS MiniportCharacteristics;
} NDIS_WRAPPER_CONTEXT, *PNDIS_WRAPPER_CONTEXT;

/* ---- NDIS miniport adapter context -------------------------------- */

typedef struct _NDIS_MINIPORT_ADAPTER {
    BOOLEAN             Initialized;
    NDIS_HANDLE         MiniportHandle;
    NDIS_HANDLE         MiniportAdapterContext;
    NDIS_HANDLE         NdisWrapperHandle;
    BOOLEAN             BusMaster;
    NDIS_INTERFACE_TYPE AdapterType;
    UCHAR               PermanentMacAddress[6];
    UCHAR               CurrentMacAddress[6];
    ULONG               MediaState;
    PNDIS_WRAPPER_CONTEXT WrapperContext;
    LIST_ENTRY          MiniportListEntry;
    LIST_ENTRY          InterruptListHead;
    PVOID               ShutdownHandler;
    PVOID               ShutdownContext;
} NDIS_MINIPORT_ADAPTER, *PNDIS_MINIPORT_ADAPTER;

/* ---- NDIS interrupt -------------------------------------------------- */

#define PIC_IRQ_BASE 0x20

typedef struct _NDIS_MINIPORT_INTERRUPT_INTERNAL {
    NDIS_HANDLE         MiniportAdapterHandle;
    ULONG               InterruptVector;
    ULONG               InterruptLevel;
    USHORT              InterruptMode;
    BOOLEAN             RequestIsr;
    BOOLEAN             SharedInterrupt;
    BOOLEAN             Active;
    LIST_ENTRY          InterruptListEntry;
} NDIS_MINIPORT_INTERRUPT_INTERNAL, *PNDIS_MINIPORT_INTERRUPT_INTERNAL;

/* ---- NDIS timer ---------------------------------------------------- */

typedef struct _NDIS_MINIPORT_TIMER_INTERNAL {
    NDIS_HANDLE         MiniportAdapterHandle;
    PNDIS_TIMER_FUNCTION TimerFunction;
    PVOID               FunctionContext;
    BOOLEAN             Set;
    BOOLEAN             IsPeriodic;
    KEVENT              TimerEvent;
} NDIS_MINIPORT_TIMER_INTERNAL, *PNDIS_MINIPORT_TIMER_INTERNAL;

/* ---- Global lists (for tracking adapters/protocols) ---------------- */

extern NDIS_HANDLE NdisMiniportListHead;
extern NDIS_HANDLE NdisProtocolListHead;

/* ---- NDIS initialization ------------------------------------------ */

VOID NTAPI NdisMInitializeWrapper(
    OUT PNDIS_HANDLE NdisWrapperHandle,
    IN  PVOID       SystemSpecific1,
    IN  PVOID       SystemSpecific2,
    IN  PVOID       SystemSpecific3);

VOID NTAPI NdisTerminateWrapper(
    IN  NDIS_HANDLE NdisWrapperHandle,
    IN  PVOID       SystemSpecific1);

/* ---- Miniport registration ----------------------------------------- */

NDIS_STATUS NTAPI NdisMRegisterMiniport(
    IN  NDIS_HANDLE                         NdisWrapperHandle,
    IN  PNDIS_MINIPORT_CHARACTERISTICS      MiniportCharacteristics,
    IN  ULONG                               CharacteristicsLength);

/* ---- Adapter attributes -------------------------------------------- */

VOID NTAPI NdisMSetAttributes(
    IN  NDIS_HANDLE         MiniportAdapterHandle,
    IN  NDIS_HANDLE         MiniportAdapterContext,
    IN  BOOLEAN             BusMaster,
    IN  NDIS_INTERFACE_TYPE AdapterType);

VOID NTAPI NdisMSetAttributesEx(
    IN  NDIS_HANDLE         MiniportAdapterHandle,
    IN  NDIS_HANDLE         MiniportAdapterContext,
    IN  ULONG               CheckForHangTimeInSeconds,
    IN  ULONG               Flags,
    IN  UCHAR               VirtualStationInfoLen);

/* ---- Interrupt ------------------------------------------------------ */

NDIS_STATUS NTAPI NdisMRegisterInterrupt(
    OUT PNDIS_MINIPORT_INTERRUPT Interrupt,
    IN  NDIS_HANDLE              MiniportAdapterHandle,
    IN  ULONG                    InterruptVector,
    IN  ULONG                    InterruptLevel,
    IN  BOOLEAN                  RequestIsr,
    IN  BOOLEAN                  SharedInterrupt,
    IN  NDIS_INTERRUPT_MODE      InterruptMode);

NDIS_STATUS NTAPI NdisMDeregisterInterrupt(
    IN  NDIS_MINIPORT_INTERRUPT Interrupt);

/* ---- Timer ---------------------------------------------------------- */

VOID NTAPI NdisMInitializeTimer(
    OUT PNDIS_MINIPORT_TIMER    Timer,
    IN  NDIS_HANDLE             MiniportAdapterHandle,
    IN  PNDIS_TIMER_FUNCTION   TimerFunction,
    IN  PVOID                   FunctionContext);

VOID NTAPI NdisMSetTimer(
    IN  PNDIS_MINIPORT_TIMER    Timer,
    IN  ULONG                   MillisecondsToDelay);

VOID NTAPI NdisMSetPeriodicTimer(
    IN  PNDIS_MINIPORT_TIMER    Timer,
    IN  ULONG                   MillisecondsPeriod);

VOID NTAPI NdisMCancelTimer(
    IN  PNDIS_MINIPORT_TIMER    Timer,
    OUT PBOOLEAN                TimerCancelled);

/* ---- I/O space ----------------------------------------------------- */

VOID NTAPI NdisMMapIoSpace(
    OUT PVOID                   *VirtualAddress,
    IN  NDIS_HANDLE              MiniportAdapterHandle,
    IN  NDIS_PHYSICAL_ADDRESS    PhysicalAddress,
    IN  ULONG                   Length);

VOID NTAPI NdisMUnmapIoSpace(
    IN  NDIS_HANDLE              MiniportAdapterHandle,
    IN  PVOID                    VirtualAddress,
    IN  ULONG                    Length);

/* ---- DMA ----------------------------------------------------------- */

NDIS_STATUS NTAPI NdisMRegisterDmaChannel(
    OUT PNDIS_HANDLE             NdisDmaHandle,
    IN  NDIS_HANDLE              MiniportAdapterHandle,
    IN  ULONG                    DmaChannelIndex,
    IN  BOOLEAN                  DmaChannelAvailable,
    IN  PNDIS_DMA_DESCRIPTION     DmaDescription,
    IN  ULONG                    MaximumLength);

VOID NTAPI NdisMDeregisterDmaChannel(
    IN  NDIS_HANDLE             NdisDmaHandle);

/* ---- Send ----------------------------------------------------------- */

VOID NTAPI NdisMSendComplete(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  PNDIS_PACKET    Packet,
    IN  NDIS_STATUS     Status);

VOID NTAPI NdisMSendResourcesAvailable(
    IN  NDIS_HANDLE     MiniportAdapterHandle);

/* ---- Receive ------------------------------------------------------- */

VOID NTAPI NdisMIndicateReceivePacket(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  PNDIS_PACKET   *Packets,
    IN  ULONG           NumberOfPackets);

VOID NTAPI NdisMReturnPacket(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  PNDIS_PACKET    Packet);

/* ---- Status -------------------------------------------------------- */

VOID NTAPI NdisMIndicateStatus(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  NDIS_STATUS     GeneralStatus,
    IN  PVOID           StatusBuffer,
    IN  ULONG           StatusBufferSize);

VOID NTAPI NdisMIndicateStatusComplete(
    IN  NDIS_HANDLE     MiniportAdapterHandle);

/* ---- Query/Set ---------------------------------------------------- */

VOID NTAPI NdisMQueryInformationComplete(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  NDIS_STATUS     Status);

VOID NTAPI NdisMSetInformationComplete(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  NDIS_STATUS     Status);

/* ---- Reset -------------------------------------------------------- */

VOID NTAPI NdisMResetComplete(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  NDIS_STATUS     Status,
    IN  BOOLEAN         AddressingReset);

/* ---- Shutdown ----------------------------------------------------- */

VOID NTAPI NdisMRegisterAdapterShutdownHandler(
    IN  NDIS_HANDLE     MiniportHandle,
    IN  PVOID           ShutdownContext,
    IN  PVOID           ShutdownHandler);

VOID NTAPI NdisMDeregisterAdapterShutdownHandler(
    IN  NDIS_HANDLE     MiniportHandle);

/* ---- Synchronization ----------------------------------------------- */

BOOLEAN NTAPI NdisMSynchronizeWithInterrupt(
    IN  NDIS_MINIPORT_INTERRUPT Interrupt,
    IN  PVOID                   SynchronizeFunction,
    IN  PVOID                   SynchronizeContext);

/* ---- Utility ------------------------------------------------------ */

VOID NTAPI NdisMSleep(
    IN  ULONG       MicrosecondsToSleep);

VOID NTAPI NdisInitializeWrapper(
    OUT PNDIS_HANDLE    NdisWrapperHandle,
    IN  PVOID           SystemSpecific1,
    IN  PVOID           SystemSpecific2,
    IN  PVOID           SystemSpecific3);

#endif /* _NDIS_H_ */