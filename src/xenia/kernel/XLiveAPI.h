/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/upnp.h"

#define RAPIDJSON_HAS_STDSTRING 1

#include <third_party/libcurl/include/curl/curl.h>
#include <third_party/rapidjson/include/rapidjson/document.h>
#include <third_party/rapidjson/include/rapidjson/prettywriter.h>
#include <third_party/rapidjson/include/rapidjson/stringbuffer.h>

namespace xe {
namespace kernel {
class XLiveAPI {
 public:
  struct memory {
    char* response{};
    size_t size = 0;
    uint64_t http_code;
  };

  struct Player {
    std::string xuid;
    // xe::be<uint64_t> xuid;
    std::string hostAddress;
    xe::be<uint64_t> machineId;
    uint16_t port;
    xe::be<uint64_t> macAddress;  // 6 Bytes
    xe::be<uint64_t> sessionId;
  };

  struct SessionJSON {
    std::string sessionid;
    xe::be<uint16_t> port;
    xe::be<uint32_t> flags;
    std::string hostAddress;
    std::string macAddress;
    xe::be<uint32_t> publicSlotsCount;
    xe::be<uint32_t> privateSlotsCount;
    xe::be<uint32_t> openPublicSlotsCount;
    xe::be<uint32_t> openPrivateSlotsCount;
    xe::be<uint32_t> filledPublicSlotsCount;
    xe::be<uint32_t> filledPrivateSlotsCount;
    std::vector<Player> players;
  };

  struct XSessionArbitrationJSON {
    xe::be<uint32_t> totalPlayers;
    std::vector<std::vector<Player>> machines;
  };

  struct XTitleServerJSON {
    std::string server_address;
    uint64_t flags;
    char server_description[200];
  };
#pragma region XSession Structs
  struct XSessionModify {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> dwFlags;
    xe::be<uint32_t> dwMaxPublicSlots;
    xe::be<uint16_t> dwMaxPrivateSlots;
  };

  struct XSessionSearchEx {
    xe::be<uint32_t> proc_index;
    xe::be<uint32_t> user_index;
    xe::be<uint32_t> num_results;
    xe::be<uint16_t> num_props;
    xe::be<uint16_t> num_ctx;
    xe::be<uint32_t> props_ptr;
    xe::be<uint32_t> ctx_ptr;
    xe::be<uint32_t> cbResultsBuffer;
    xe::be<uint32_t> pSearchResults;
    xe::be<uint32_t> num_users;
  };

  struct XSessionDetails {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> details_buffer_size;
    xe::be<uint32_t> details_buffer;
    xe::be<uint32_t> pXOverlapped;
  };

  struct XSessionMigate {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> user_index;
    xe::be<uint32_t> session_info;
    xe::be<uint32_t> pXOverlapped;
  };

  struct XSessionArbitrationData {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> flags;
    xe::be<uint32_t> unk1;
    xe::be<uint32_t> unk2;
    xe::be<uint32_t> session_nonce;
    xe::be<uint32_t> results_buffer_length;
    xe::be<uint32_t> results_buffer;
    xe::be<uint32_t> pXOverlapped;
  };

  struct XSesion {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> flags;
    xe::be<uint32_t> num_slots_public;
    xe::be<uint32_t> num_slots_private;
    xe::be<uint32_t> user_index;
    xe::be<uint32_t> session_info_ptr;
    xe::be<uint32_t> nonce_ptr;
  };

  struct XSessionWriteStats {
    xe::be<uint32_t> session_handle;
    xe::be<uint32_t> unk1;
    xe::be<uint64_t> xuid;
    xe::be<uint32_t> number_of_leaderboards;
    xe::be<uint32_t> leaderboards_guest_address;
  };

  struct XSessionViewProperties {
    xe::be<uint32_t> leaderboard_id;
    xe::be<uint32_t> properties_count;
    xe::be<uint32_t> properties_guest_address;
  };

  struct XUSER_DATA {
    uint8_t type;

    union {
      xe::be<uint32_t> dword_data;  // XUSER_DATA_TYPE_INT32
      xe::be<uint64_t> qword_data;  // XUSER_DATA_TYPE_INT64
      xe::be<double> double_data;   // XUSER_DATA_TYPE_DOUBLE
      struct                        // XUSER_DATA_TYPE_UNICODE
      {
        xe::be<uint32_t> string_length;
        xe::be<uint32_t> string_ptr;
      } string;
      xe::be<float> float_data;
      struct {
        xe::be<uint32_t> data_length;
        xe::be<uint32_t> data_ptr;
      } binary;
      FILETIME filetime_data;
    };
  };

  struct XUSER_PROPERTY {
    xe::be<uint32_t> property_id;
    XUSER_DATA value;
  };

  struct XTitleServer {
    in_addr server_address;
    uint64_t flags;
    char server_description[200];
  };
#pragma endregion

  //~XLiveAPI() {
  //  // upnp_handler.~upnp();
  //}

  static bool is_active();

  static std::string GetApiAddress();

  static uint32_t GetNatType();

  static bool IsOnline();

  static uint16_t GetPlayerPort();

  static void Init();

  static void RandomBytes(unsigned char* buffer_ptr, uint32_t length);

  static void clearXnaddrCache();

  static sockaddr_in Getwhoami();

  static sockaddr_in GetLocalIP();

  static const std::string ip_to_string(sockaddr_in sockaddr);

  static void DownloadPortMappings();

  static xe::be<uint64_t> MacAddresstoUint64(const unsigned char* macAddress);

  static void Uint64toSessionId(xe::be<uint64_t> sessionID,
                                unsigned char* sessionIdOut);

  static void Uint64toMacAddress(xe::be<uint64_t> macAddress,
                                 unsigned char* macAddressOut);

  static uint64_t GetMachineId();

  static void RegisterPlayer();

  static uint64_t hex_to_uint64(const char* hex);

  static XLiveAPI::Player FindPlayers();

  static void QoSPost(xe::be<uint64_t> sessionId, char* qosData,
                     size_t qosLength);

  static XLiveAPI::memory QoSGet(xe::be<uint64_t> sessionId);

  static void SessionModify(xe::be<uint64_t> sessionId, XSessionModify* data);

  static const std::vector<XLiveAPI::SessionJSON> SessionSearchEx(
      XSessionSearchEx* data);

  static const XLiveAPI::SessionJSON SessionDetails(xe::be<uint64_t> sessionId);

  static XLiveAPI::SessionJSON XSessionMigration(xe::be<uint64_t> sessionId);

  static char* XSessionArbitration(xe::be<uint64_t> sessionId);

  static void XLiveAPI::SessionWriteStats(xe::be<uint64_t> sessionId,
                                          XSessionWriteStats* stats,
                                          XSessionViewProperties* probs);

  static XLiveAPI::memory LeaderboardsFind(const char* data);

  static void DeleteSession(xe::be<uint64_t> sessionId);

  static void DeleteAllSessions();

  static void XSessionCreate(xe::be<uint64_t> sessionId, XSesion* data);

  static XLiveAPI::SessionJSON XSessionGet(xe::be<uint64_t> sessionId);

  static std::vector<XLiveAPI::XTitleServerJSON> GetServers();

  static void SessionJoinRemote(xe::be<uint64_t> sessionId, const char* data);

  static void SessionLeaveRemote(xe::be<uint64_t> sessionId, const char* data);

  static unsigned char* GenerateMacAddress();

  static unsigned char* GetMACaddress();

  // static void resetQosCache();

  static const sockaddr_in LocalIP() { return local_ip_; };
  static const sockaddr_in OnlineIP() { return online_ip_; };

  static const std::string LocalIP_str() { return ip_to_string(local_ip_); };
  static const std::string OnlineIP_str() { return ip_to_string(online_ip_); };

  inline static upnp upnp_handler;

  inline static unsigned char* mac_address = new unsigned char[6];

  inline static std::map<xe::be<uint32_t>, xe::be<uint64_t>> sessionHandleMap{};

  inline static std::map<xe::be<uint32_t>, xe::be<uint64_t>> machineIdCache{};
  inline static std::map<xe::be<uint32_t>, xe::be<uint64_t>> sessionIdCache{};
  inline static std::map<xe::be<uint32_t>, xe::be<uint64_t>> macAddressCache{};

 private:
  inline static bool active_ = false;

  // std::shared_mutex mutex_;

  static memory Get(std::string endpoint);

  static memory Post(std::string endpoint, const char* data,
                     size_t data_size = 0);

  static memory Delete(std::string endpoint);

  // https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
  static size_t callback(void* data, size_t size, size_t nmemb, void* clientp) {
    size_t realsize = size * nmemb;
    struct memory* mem = (struct memory*)clientp;

    char* ptr = (char*)realloc(mem->response, mem->size + realsize + 1);
    if (ptr == NULL) return 0; /* out of memory! */

    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), data, realsize);
    mem->size += realsize;
    mem->response[mem->size] = 0;

    return realsize;
  };

  inline static sockaddr_in online_ip_;

  inline static sockaddr_in local_ip_;
};
}  // namespace kernel
}  // namespace xe
