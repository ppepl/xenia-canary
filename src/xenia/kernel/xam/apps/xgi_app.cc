/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/apps/xgi_app.h"
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"

#ifdef XE_PLATFORM_WIN32
// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <WS2tcpip.h>                    // NOLINT(build/include_order)
#include <winsock2.h>                    // NOLINT(build/include_order)
#endif

#include <random>

// Get title id
#include <src/xenia/kernel/kernel_state.cc>

#include <xenia/kernel/XLiveAPI.h>

using namespace rapidjson;
using namespace xe::string_util;

DECLARE_bool(logging);

namespace xe {
namespace kernel {
namespace xam {
namespace apps {
/*
 * Most of the structs below were found in the Source SDK, provided as stubs.
 * Specifically, they can be found in the Source 2007 SDK and the Alien Swarm
 * Source SDK. Both are available on Steam for free. A GitHub mirror of the
 * Alien Swarm SDK can be found here:
 * https://github.com/NicolasDe/AlienSwarm/blob/master/src/common/xbox/xboxstubs.h
 */

struct X_XUSER_ACHIEVEMENT {
  xe::be<uint32_t> user_idx;
  xe::be<uint32_t> achievement_id;
};

struct XSESSION_REGISTRATION_RESULTS {
  xe::be<uint32_t> registrants_count;
  xe::be<uint32_t> registrants_ptr;
};

struct XSESSION_REGISTRANT {
  xe::be<uint64_t> qwMachineID;
  xe::be<uint32_t> bTrustworthiness;
  xe::be<uint32_t> bNumUsers;
  xe::be<uint32_t> rgUsers;
};

XgiApp::XgiApp(KernelState* kernel_state) : App(kernel_state, 0xFB) {}

// http://mb.mirage.org/bugzilla/xliveless/main.c

struct XNKID {
  uint8_t ab[8];
};

struct XNKEY {
  uint8_t ab[16];
};

struct XNADDR {
  in_addr ina;
  in_addr inaOnline;
  xe::be<uint16_t> wPortOnline;
  uint8_t abEnet[6];
  uint8_t abOnline[20];
};

struct XSESSION_INFO {
  XNKID sessionID;
  XNADDR hostAddress;
  XNKEY keyExchangeKey;
};

struct XUSER_CONTEXT {
  xe::be<uint32_t> context_id;
  xe::be<uint32_t> value;
};

struct XSESSION_SEARCHRESULT {
  XSESSION_INFO info;
  xe::be<uint32_t> open_public_slots;
  xe::be<uint32_t> open_priv_slots;
  xe::be<uint32_t> filled_public_slots;
  xe::be<uint32_t> filled_priv_slots;
  xe::be<uint32_t> properties_count;
  xe::be<uint32_t> contexts_count;
  xe::be<uint32_t> properties_ptr;
  xe::be<uint32_t> contexts_ptr;
};

struct XSESSION_SEARCHRESULT_HEADER {
  xe::be<uint32_t> search_results_count;
  xe::be<uint32_t> search_results_ptr;
};

// Set only on the host of a multiplayer session.
// The user who sets the host flag is the user that interacts with Live
#define XSESSION_CREATE_HOST 0x00000001
// Session is used across games to keep players together.
// Advertises state via Presence
#define XSESSION_CREATE_USES_PRESENCE 0x00000002
// Session is used for stats tracking
#define XSESSION_CREATE_USES_STATS 0x00000004
// Session is advertised in matchmaking for searching
#define XSESSION_CREATE_USES_MATCHMAKING 0x00000008
// Session stats are arbitrated (and therefore tracked for
// everyone in the game)
#define XSESSION_CREATE_USES_ARBITRATION 0x00000010
// Session XNKey is registered and PC settings are enforced
#define XSESSION_CREATE_USES_PEER_NETWORK 0x00000020
// Session may be converted to an Social Matchmaking session
#define XSESSION_CREATE_SOCIAL_MATCHMAKING_ALLOWED 0x00000080
// Game Invites cannot be sent by the HUD for this session
#define XSESSION_CREATE_INVITES_DISABLED 0x00000100
// Session will not ever be displayed as joinable via Presence
#define XSESSION_CREATE_JOIN_VIA_PRESENCE_DISABLED 0x00000200
// Session will not be joinable between XSessionStart and XSessionEnd
#define XSESSION_CREATE_JOIN_IN_PROGRESS_DISABLED 0x00000400
// Session is only joinable via presence for friends of the host
#define XSESSION_CREATE_JOIN_VIA_PRESENCE_FRIENDS_ONLY 0x00000800

const uint32_t XSESSION_CREATE_SINGLEPLAYER_WITH_STATS =
    XSESSION_CREATE_USES_PRESENCE | XSESSION_CREATE_USES_STATS |
    XSESSION_CREATE_INVITES_DISABLED |
    XSESSION_CREATE_JOIN_VIA_PRESENCE_DISABLED |
    XSESSION_CREATE_JOIN_IN_PROGRESS_DISABLED;

const uint32_t XSESSION_CREATE_LIVE_MULTIPLAYER_STANDARD =
    XSESSION_CREATE_USES_PRESENCE | XSESSION_CREATE_USES_STATS |
    XSESSION_CREATE_USES_MATCHMAKING | XSESSION_CREATE_USES_PEER_NETWORK;

const uint32_t XSESSION_CREATE_LIVE_MULTIPLAYER_RANKED =
    XSESSION_CREATE_LIVE_MULTIPLAYER_STANDARD |
    XSESSION_CREATE_USES_ARBITRATION;

const uint32_t XSESSION_CREATE_SYSTEMLINK = XSESSION_CREATE_USES_PEER_NETWORK;

const uint32_t XSESSION_CREATE_GROUP_LOBBY =
    XSESSION_CREATE_USES_PRESENCE | XSESSION_CREATE_USES_PEER_NETWORK;

const uint32_t XSESSION_CREATE_GROUP_GAME = XSESSION_CREATE_USES_STATS |
                                            XSESSION_CREATE_USES_MATCHMAKING |
                                            XSESSION_CREATE_USES_PEER_NETWORK;

enum XSESSION_STATE : uint32_t {
  XSESSION_STATE_LOBBY,
  XSESSION_STATE_REGISTRATION,
  XSESSION_STATE_INGAME,
  XSESSION_STATE_REPORTING,
  XSESSION_STATE_DELETED,
};

struct XSESSION_LOCAL_DETAILS {
  xe::be<uint32_t> dwUserIndexHost;
  xe::be<uint32_t> dwGameType;
  xe::be<uint32_t> dwGameMode;
  xe::be<uint32_t> dwFlags;
  xe::be<uint32_t> dwMaxPublicSlots;
  xe::be<uint32_t> dwMaxPrivateSlots;
  xe::be<uint32_t> dwAvailablePublicSlots;
  xe::be<uint32_t> dwAvailablePrivateSlots;
  xe::be<uint32_t> dwActualMemberCount;
  xe::be<uint32_t> dwReturnedMemberCount;
  XSESSION_STATE eState;
  xe::be<uint64_t> qwNonce;
  XSESSION_INFO sessionInfo;
  XNKID xnkidArbitration;
  xe::be<uint32_t> pSessionMembers;
};

struct XSESSION_MEMBER {
  xe::be<uint64_t> xuidOnline;
  xe::be<uint32_t> dwUserIndex;
  xe::be<uint32_t> dwFlags;
};

// TODO: Move - Codie
bool StringToHex(const std::string& inStr, unsigned char* outStr) {
  size_t len = inStr.length();
  for (size_t i = 0; i < len; i += 2) {
    sscanf(inStr.c_str() + i, "%2hhx", outStr);
    ++outStr;
  }
  return true;
}

xe::be<uint64_t> XNKIDtoUint64(XNKID* sessionID) {
  int i;
  xe::be<uint64_t> sessionId64 = 0;
  for (i = 7; i >= 0; --i) {
    sessionId64 = sessionId64 << 8;
    sessionId64 |= (uint64_t)sessionID->ab[7 - i];
  }

  return sessionId64;
}

void Uint64toXNKID(xe::be<uint64_t> sessionID, XNKID* xnkid) {
  for (int i = 0; i < 8; i++) {
    xnkid->ab[i] = ((sessionID >> (8 * i)) & 0XFF);
  }
}

xe::be<uint64_t> UCharArrayToUint64(unsigned char* data) {
  int i;
  xe::be<uint64_t> out = 0;
  for (i = 7; i >= 0; --i) {
    out = out << 8;
    out |= (uint64_t)data[7 - i];
  }

  return out;
}

struct XUSER_STATS_READ_RESULTS {
  xe::be<uint32_t> dwNumViews;
  xe::be<uint32_t> pViews;
};

struct XUSER_STATS_VIEW {
  xe::be<uint32_t> dwViewId;
  xe::be<uint32_t> dwTotalViewRows;
  xe::be<uint32_t> dwNumRows;
  xe::be<uint32_t> pRows;
};

struct XUSER_STATS_ROW {
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> dwRank;
  xe::be<uint64_t> i64Rating;
  CHAR szGamertag[16];
  xe::be<uint32_t> dwNumColumns;
  xe::be<uint32_t> pColumns;
};

struct XUSER_STATS_COLUMN {
  xe::be<uint16_t> wColumnId;
  XLiveAPI::XUSER_DATA Value;
};

struct XUSER_STATS_SPEC {
  xe::be<uint32_t> dwViewId;
  xe::be<uint32_t> dwNumColumnIds;
  xe::be<uint16_t> rgwColumnIds[0x40];
};

X_HRESULT XgiApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);

  switch (message) {
    case 0x000B0018: {
      XLiveAPI::XSessionModify* data =
          reinterpret_cast<XLiveAPI::XSessionModify*>(buffer);

      XELOGI("XLiveAPI::XSessionModify({:08X} {:08X} {:08X} {:08X})",
             data->session_handle, data->dwFlags, data->dwMaxPublicSlots,
             data->dwMaxPrivateSlots);

      XLiveAPI::SessionModify(XLiveAPI::sessionHandleMap[data->session_handle], data);

      return X_E_SUCCESS;
    }
    case 0x000B0016:  // XSessionSearch
    case 0x000B001C: {
      XELOGI("XSessionSearchEx");

      XLiveAPI::XSessionSearchEx* data =
          reinterpret_cast<XLiveAPI::XSessionSearchEx*>(buffer);

      auto* pSearchContexts =
          memory_->TranslateVirtual<XUSER_CONTEXT*>(data->ctx_ptr);

      uint32_t results_ptr =
          data->pSearchResults + sizeof(XSESSION_SEARCHRESULT_HEADER);

      auto* result =
          memory_->TranslateVirtual<XSESSION_SEARCHRESULT*>(results_ptr);

      auto resultsHeader =
          memory_->TranslateVirtual<XSESSION_SEARCHRESULT_HEADER*>(
              data->pSearchResults);

      // return a list of session for the title
      const std::vector<XLiveAPI::SessionJSON> sessions =
          XLiveAPI::SessionSearchEx(data);

      uint32_t i = 0;
      for (const auto& session : sessions) {
        uint32_t result_guest_address = data->pSearchResults +
                                        sizeof(XSESSION_SEARCHRESULT_HEADER) +
                                        (sizeof(XSESSION_SEARCHRESULT) * i);

        auto* resultHostPtr = memory_->TranslateVirtual<XSESSION_SEARCHRESULT*>(
            result_guest_address);

        // If we have looped through all sessions then exit.
        if (data->num_results <= i) break;

        result[i].contexts_count = (uint32_t)data->num_ctx;
        result[i].properties_count = 3;
        result[i].contexts_ptr = data->ctx_ptr;
        result[i].properties_ptr = data->props_ptr;

        result[i].filled_priv_slots = session.filledPrivateSlotsCount;

        result[i].filled_public_slots = session.filledPublicSlotsCount;
        result[i].open_priv_slots = session.openPrivateSlotsCount;
        result[i].open_public_slots = session.openPublicSlotsCount;

        xe::be<uint64_t> sessionId =
            XLiveAPI::hex_to_uint64(session.sessionid.c_str());
        xe::be<uint64_t> macAddress =
            XLiveAPI::hex_to_uint64(session.macAddress.c_str());

        memcpy(&result[i].info.sessionID, &sessionId, 8);
        memcpy(&result[i].info.hostAddress.abEnet, &macAddress, 6);
        memcpy(&result[i].info.hostAddress.abOnline, &macAddress, 6);

        for (int j = 0; j < 16; j++) {
          result[i].info.keyExchangeKey.ab[j] = j;
        }

        inet_pton(AF_INET, session.hostAddress.c_str(),
                  &resultHostPtr[i].info.hostAddress.ina.s_addr);

        inet_pton(AF_INET, session.hostAddress.c_str(),
                  &resultHostPtr[i].info.hostAddress.inaOnline.s_addr);

        resultHostPtr[i].info.hostAddress.wPortOnline = session.port;

        i += 1;

        resultsHeader->search_results_count = i;
        resultsHeader->search_results_ptr =
            data->pSearchResults + sizeof(XSESSION_SEARCHRESULT_HEADER);
      }

      return X_E_SUCCESS;
    }
    case 0x000B001D: {
      XLiveAPI::XSessionDetails* data =
          reinterpret_cast<XLiveAPI::XSessionDetails*>(buffer);

      XELOGI("XSessionGetDetails({:08X});", buffer_length);

      auto details = memory_->TranslateVirtual<XSESSION_LOCAL_DETAILS*>(
          data->details_buffer);

      XLiveAPI::SessionJSON session =
          XLiveAPI::SessionDetails(XLiveAPI::sessionHandleMap[data->session_handle]);

      if (session.hostAddress.empty()) {
        return 1;
      }

      memcpy(&details->sessionInfo.sessionID, session.sessionid.c_str(), 8);

      details->sessionInfo.hostAddress.inaOnline.s_addr =
          inet_addr(session.hostAddress.c_str());
      details->sessionInfo.hostAddress.ina.s_addr =
          details->sessionInfo.hostAddress.inaOnline.s_addr;

      memcpy(&details->sessionInfo.hostAddress.abEnet,
             session.hostAddress.c_str(), 6);
      details->sessionInfo.hostAddress.wPortOnline = session.port;

      details->dwUserIndexHost = 0;
      details->dwGameMode = 0;
      details->dwGameType = 0;
      details->eState = XSESSION_STATE_LOBBY;
      // details->eState = session.state;

      details->dwFlags = session.flags;
      details->dwMaxPublicSlots = session.publicSlotsCount;
      details->dwMaxPrivateSlots = session.privateSlotsCount;
      details->dwAvailablePrivateSlots = session.openPublicSlotsCount;
      details->dwAvailablePublicSlots = session.openPrivateSlotsCount;
      details->dwActualMemberCount =
          session.filledPublicSlotsCount + session.filledPrivateSlotsCount;
      details->dwReturnedMemberCount = (uint32_t)session.players.size();

      details->qwNonce = 0xAAAAAAAAAAAAAAAA;

      for (int i = 0; i < 16; i++) {
        details->sessionInfo.keyExchangeKey.ab[i] = i;
      }

      for (int i = 0; i < 20; i++) {
        details->sessionInfo.hostAddress.abOnline[i] = i;
      }

      uint32_t members_ptr = memory_->SystemHeapAlloc(
          sizeof(XSESSION_MEMBER) * details->dwReturnedMemberCount);

      auto members = memory_->TranslateVirtual<XSESSION_MEMBER*>(members_ptr);
      details->pSessionMembers = members_ptr;

      unsigned int i = 0;

      for (const auto& player : session.players) {
        members[i].dwUserIndex = 0xFE;

        int size = sizeof(player.xuid);
        memcpy(&members[i].xuidOnline, player.xuid.c_str(), size);

        i += 1;
      }

      return X_E_SUCCESS;
    }
    case 0x000B001E: {
      XLiveAPI::XSessionMigate* data =
          reinterpret_cast<XLiveAPI::XSessionMigate*>(buffer);

      XELOGI("XSessionMigrateHost({:08X});", buffer_length);

      auto sessionInfo =
          memory_->TranslateVirtual<XSESSION_INFO*>(data->session_info);

      XLiveAPI::SessionJSON result =
          XLiveAPI::XSessionMigration(XLiveAPI::sessionHandleMap[data->session_handle]);

      sessionInfo->hostAddress.inaOnline.s_addr =
          inet_addr(result.hostAddress.c_str());
      sessionInfo->hostAddress.ina.s_addr =
          sessionInfo->hostAddress.inaOnline.s_addr;

      memcpy(&sessionInfo->hostAddress.abEnet, result.macAddress.c_str(), 6);

      sessionInfo->hostAddress.wPortOnline = result.port;

      return X_E_SUCCESS;
    }
    case 0x000B0021: {
      struct XLeaderboard {
        xe::be<uint32_t> titleId;
        xe::be<uint32_t> xuids_count;
        xe::be<uint32_t> xuids_guest_address;
        xe::be<uint32_t> specs_count;
        xe::be<uint32_t> specs_guest_address;
        xe::be<uint32_t> results_size;
        xe::be<uint32_t> results_guest_address;
      }* data = reinterpret_cast<XLeaderboard*>(buffer);

      if (!data->results_guest_address) return 1;

#pragma region Curl
      Document d;
      d.SetObject();

      Document::AllocatorType& allocator = d.GetAllocator();

      size_t sz = allocator.Size();

      rapidjson::Value xuidsJsonArray(rapidjson::kArrayType);
      auto xuids = memory_->TranslateVirtual<xe::be<uint64_t>*>(
          data->xuids_guest_address);

      for (unsigned int playerIndex = 0; playerIndex < data->xuids_count;
           playerIndex++) {
        std::stringstream xuidSS;
        xuidSS << std::hex << std::noshowbase << std::setw(16)
               << std::setfill('0') << xuids[playerIndex];
        rapidjson::Value value;
        value.SetString(xuidSS.str().c_str(), 16, allocator);
        xuidsJsonArray.PushBack(value, allocator);
      }

      d.AddMember("players", xuidsJsonArray, allocator);

      std::stringstream titleId;
      titleId << std::hex << std::noshowbase << std::setw(8)
              << std::setfill('0') << kernel_state()->title_id();

      d.AddMember("titleId", titleId.str(), allocator);

      rapidjson::Value leaderboardQueryJsonArray(rapidjson::kArrayType);
      auto queries = memory_->TranslateVirtual<XUSER_STATS_SPEC*>(
          data->specs_guest_address);

      for (unsigned int queryIndex = 0; queryIndex < data->specs_count;
           queryIndex++) {
        rapidjson::Value queryObject(rapidjson::kObjectType);
        queryObject.AddMember("id", queries[queryIndex].dwViewId, allocator);
        rapidjson::Value statIdsArray(rapidjson::kArrayType);
        for (uint32_t statIdIndex = 0;
             statIdIndex < queries[queryIndex].dwNumColumnIds; statIdIndex++) {
          statIdsArray.PushBack(queries[queryIndex].rgwColumnIds[statIdIndex],
                                allocator);
        }
        queryObject.AddMember("statisticIds", statIdsArray, allocator);
        leaderboardQueryJsonArray.PushBack(queryObject, allocator);
      }

      d.AddMember("queries", leaderboardQueryJsonArray, allocator);

      rapidjson::StringBuffer buffer;
      PrettyWriter<rapidjson::StringBuffer> writer(buffer);
      d.Accept(writer);

      XLiveAPI::memory chunk = XLiveAPI::LeaderboardsFind(buffer.GetString());

      rapidjson::Document leaderboards;
      leaderboards.Parse(chunk.response);
      const Value& leaderboardsArray = leaderboards.GetArray();

      auto leaderboards_guest_address = memory_->SystemHeapAlloc(
          sizeof(XUSER_STATS_VIEW) * leaderboardsArray.Size());
      auto leaderboard = memory_->TranslateVirtual<XUSER_STATS_VIEW*>(
          leaderboards_guest_address);
      auto resultsHeader = memory_->TranslateVirtual<XUSER_STATS_READ_RESULTS*>(
          data->results_guest_address);
      resultsHeader->dwNumViews = leaderboardsArray.Size();
      resultsHeader->pViews = leaderboards_guest_address;

      uint32_t leaderboardIndex = 0;
      for (Value::ConstValueIterator leaderboardObjectPtr =
               leaderboardsArray.Begin();
           leaderboardObjectPtr != leaderboardsArray.End();
           ++leaderboardObjectPtr) {
        leaderboard[leaderboardIndex].dwViewId =
            (*leaderboardObjectPtr)["id"].GetInt();
        auto playersArray = (*leaderboardObjectPtr)["players"].GetArray();
        leaderboard[leaderboardIndex].dwNumRows = playersArray.Size();
        leaderboard[leaderboardIndex].dwTotalViewRows = playersArray.Size();
        auto players_guest_address = memory_->SystemHeapAlloc(
            sizeof(XUSER_STATS_ROW) * playersArray.Size());
        auto player =
            memory_->TranslateVirtual<XUSER_STATS_ROW*>(players_guest_address);
        leaderboard[leaderboardIndex].pRows = players_guest_address;

        uint32_t playerIndex = 0;
        for (Value::ConstValueIterator playerObjectPtr = playersArray.Begin();
             playerObjectPtr != playersArray.End(); ++playerObjectPtr) {
          player[playerIndex].dwRank = 1;
          player[playerIndex].i64Rating = 1;
          auto gamertag = (*playerObjectPtr)["gamertag"].GetString();
          auto gamertagLength =
              (*playerObjectPtr)["gamertag"].GetStringLength();
          memcpy(player[playerIndex].szGamertag, gamertag, gamertagLength);
          unsigned char xuid[8];
          StringToHex((*playerObjectPtr)["xuid"].GetString(), xuid);
          player[playerIndex].xuid = UCharArrayToUint64(xuid);

          auto statisticsArray = (*playerObjectPtr)["stats"].GetArray();
          player[playerIndex].dwNumColumns = statisticsArray.Size();
          auto stats_guest_address = memory_->SystemHeapAlloc(
              sizeof(XUSER_STATS_COLUMN) * statisticsArray.Size());
          auto stat = memory_->TranslateVirtual<XUSER_STATS_COLUMN*>(
              stats_guest_address);
          player[playerIndex].pColumns = stats_guest_address;

          uint32_t statIndex = 0;
          for (Value::ConstValueIterator statObjectPtr =
                   statisticsArray.Begin();
               statObjectPtr != statisticsArray.End(); ++statObjectPtr) {
            stat[statIndex].wColumnId = (*statObjectPtr)["id"].GetUint();
            stat[statIndex].Value.type = (*statObjectPtr)["type"].GetUint();

            switch (stat[statIndex].Value.type) {
              case 1:
                stat[statIndex].Value.dword_data =
                    (*statObjectPtr)["value"].GetUint();
                break;
              case 2:
                stat[statIndex].Value.qword_data =
                    (*statObjectPtr)["value"].GetUint64();
                break;
              default:
                XELOGW("Unimplemented stat type for read, will attempt anyway.",
                       stat[statIndex].Value.type);
                if ((*statObjectPtr)["value"].IsNumber())
                  stat[statIndex].Value.qword_data =
                      (*statObjectPtr)["value"].GetUint64();
            }

            stat[statIndex].Value.type = (*statObjectPtr)["type"].GetInt();
            statIndex++;
          }

          playerIndex++;
        }

        leaderboardIndex++;
      }
#pragma endregion
      return X_E_SUCCESS;
    }
    case 0x000B001A: {
      XLiveAPI::XSessionArbitrationData* data =
          reinterpret_cast<XLiveAPI::XSessionArbitrationData*>(buffer);

      XELOGI(
          "XSessionArbitrationRegister({:08X}, {:08X}, {:08X}, {:08X}, {:08X}, "
          "{:08X}, {:08X}, {:08X});",
          data->session_handle, data->flags, data->unk1, data->unk2,
          data->session_nonce, data->results_buffer_length,
          data->results_buffer, data->pXOverlapped);

      auto results = memory_->TranslateVirtual<XSESSION_REGISTRATION_RESULTS*>(
          data->results_buffer);

      // TODO: Remove hardcoded results, populate properly.

      char* result =
          XLiveAPI::XSessionArbitration(XLiveAPI::sessionHandleMap[data->session_handle]);

#pragma region Curl
      rapidjson::Document d;
      d.Parse(result);

      auto machinesArray = d["machines"].GetArray();

      uint32_t registrants_ptr = memory_->SystemHeapAlloc(
          sizeof(XSESSION_REGISTRANT) * machinesArray.Size());

      uint32_t users_ptr = memory_->SystemHeapAlloc(sizeof(uint64_t) *
                                                    d["totalPlayers"].GetInt());

      auto registrants =
          memory_->TranslateVirtual<XSESSION_REGISTRANT*>(registrants_ptr);

      auto users = memory_->TranslateVirtual<xe::be<uint64_t>*>(users_ptr);

      results->registrants_ptr = registrants_ptr;
      results->registrants_count = machinesArray.Size();

      unsigned int machineIndex = 0;
      unsigned int machinePlayerIndex = 0;
      unsigned int resultsPlayerIndex = 0;

      for (const auto& machine : machinesArray) {
        auto playersArray = machine["players"].GetArray();
        registrants[machineIndex].bNumUsers = playersArray.Size();
        registrants[machineIndex].bTrustworthiness = 1;
        unsigned char machineId[8];
        StringToHex(machine["id"].GetString(), machineId);
        registrants[machineIndex].qwMachineID = UCharArrayToUint64(machineId);
        registrants[machineIndex].rgUsers =
            users_ptr + (8 * resultsPlayerIndex);

        machinePlayerIndex = 0;
        for (const auto& player : playersArray) {
          unsigned char xuid[8];
          StringToHex(player["xuid"].GetString(), xuid);

          users[resultsPlayerIndex] = UCharArrayToUint64(xuid);

          machinePlayerIndex += 1;
          resultsPlayerIndex += 1;
        }

        machineIndex += 1;
      }
#pragma endregion

      return X_E_SUCCESS;
    }
    case 0x000B0006: {
      assert_true(!buffer_length || buffer_length == 24);

      // dword r3 user index
      // dword (unwritten?)
      // qword 0
      // dword r4 context enum
      // dword r5 value
      uint32_t user_index = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t context_id = xe::load_and_swap<uint32_t>(buffer + 16);
      uint32_t context_value = xe::load_and_swap<uint32_t>(buffer + 20);
      XELOGD("XGIUserSetContextEx({:08X}, {:08X}, {:08X})", user_index,
             context_id, context_value);
      return X_E_SUCCESS;
    }
    case 0x000B0007: {
      uint32_t user_index = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t property_id = xe::load_and_swap<uint32_t>(buffer + 16);
      uint32_t value_size = xe::load_and_swap<uint32_t>(buffer + 20);
      uint32_t value_ptr = xe::load_and_swap<uint32_t>(buffer + 24);
      XELOGD("XGIUserSetPropertyEx({:08X}, {:08X}, {}, {:08X})", user_index,
             property_id, value_size, value_ptr);
      return X_E_SUCCESS;
    }
    case 0x000B0008: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t achievement_count = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t achievements_ptr = xe::load_and_swap<uint32_t>(buffer + 4);
      XELOGD("XGIUserWriteAchievements({:08X}, {:08X})", achievement_count,
             achievements_ptr);

      auto* achievement =
          (X_XUSER_ACHIEVEMENT*)memory_->TranslateVirtual(achievements_ptr);
      for (uint32_t i = 0; i < achievement_count; i++, achievement++) {
        kernel_state_->achievement_manager()->EarnAchievement(
            achievement->user_idx, 0, achievement->achievement_id);
      }
      return X_E_SUCCESS;
    }
    case 0x000B0010: {
      XELOGI("XSessionCreate");

      assert_true(!buffer_length || buffer_length == 28);
      // Sequence:
      // - XamSessionCreateHandle
      // - XamSessionRefObjByHandle
      // - [this]
      // - CloseHandle

      XLiveAPI::XSesion* data = reinterpret_cast<XLiveAPI::XSesion*>(buffer);

      std::random_device rd;
      std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFFu);

      auto* pSessionInfo =
          memory_->TranslateVirtual<XSESSION_INFO*>(data->session_info_ptr);

      for (int i = 0; i < 16; i++) {
        pSessionInfo->keyExchangeKey.ab[i] = i;
      }

      switch (data->flags) {
        case XSESSION_CREATE_SINGLEPLAYER_WITH_STATS:
          XELOGI("XSessionCreate XSESSION_CREATE_SINGLEPLAYER_WITH_STATS");
          break;
        case XSESSION_CREATE_LIVE_MULTIPLAYER_STANDARD:
          XELOGI("XSessionCreate XSESSION_CREATE_LIVE_MULTIPLAYER_STANDARD");
          break;
        case XSESSION_CREATE_LIVE_MULTIPLAYER_RANKED:
          XELOGI("XSessionCreate XSESSION_CREATE_LIVE_MULTIPLAYER_RANKED");
          break;
        case XSESSION_CREATE_SYSTEMLINK:
          XELOGI("XSessionCreate XSESSION_CREATE_SYSTEMLINK");
          break;
        case XSESSION_CREATE_GROUP_LOBBY:
          XELOGI("XSessionCreate XSESSION_CREATE_GROUP_LOBBY");
          break;
        case XSESSION_CREATE_GROUP_GAME:
          XELOGI("XSessionCreate XSESSION_CREATE_GROUP_GAME");
          break;
        default:
          break;
      }

      if (data->flags & XSESSION_CREATE_HOST) {
        XELOGI("XSESSION_CREATE_HOST Set");
      }

      if (data->flags & XSESSION_CREATE_USES_PRESENCE) {
        XELOGI("XSESSION_CREATE_USES_PRESENCE Set");
      }

      if (data->flags & XSESSION_CREATE_USES_STATS) {
        XELOGI("XSESSION_CREATE_USES_STATS Set");
      }

      if (data->flags & XSESSION_CREATE_USES_MATCHMAKING) {
        XELOGI("XSESSION_CREATE_USES_MATCHMAKING Set");
      }

      if (data->flags & XSESSION_CREATE_USES_ARBITRATION) {
        XELOGI("XSESSION_CREATE_USES_ARBITRATION Set");
      }

      if (data->flags & XSESSION_CREATE_USES_PEER_NETWORK) {
        XELOGI("XSESSION_CREATE_USES_PEER_NETWORK Set");
      }

      if (data->flags & XSESSION_CREATE_SOCIAL_MATCHMAKING_ALLOWED) {
        XELOGI("XSESSION_CREATE_SOCIAL_MATCHMAKING_ALLOWED Set");
      }

      if (data->flags & XSESSION_CREATE_INVITES_DISABLED) {
        XELOGI("XSESSION_CREATE_INVITES_DISABLED Set");
      }

      if (data->flags & XSESSION_CREATE_JOIN_VIA_PRESENCE_DISABLED) {
        XELOGI("XSESSION_CREATE_JOIN_VIA_PRESENCE_DISABLED Set");
      }

      if (data->flags & XSESSION_CREATE_JOIN_IN_PROGRESS_DISABLED) {
        XELOGI("XSESSION_CREATE_JOIN_IN_PROGRESS_DISABLED Set");
      }

      if (data->flags & XSESSION_CREATE_JOIN_VIA_PRESENCE_FRIENDS_ONLY) {
        XELOGI("XSESSION_CREATE_JOIN_VIA_PRESENCE_FRIENDS_ONLY Set");
      }

      if (data->flags == XSESSION_CREATE_USES_STATS) {
        // Update Stats
        XELOGI("XSESSION_CREATE_USES_STATS Unimplemented");

        return X_E_SUCCESS;
      }

      // If host
      if (data->flags & XSESSION_CREATE_HOST) {
        // Generate a random session id.
        Uint64toXNKID(dist(rd), &pSessionInfo->sessionID);
        *memory_->TranslateVirtual<uint64_t*>(data->nonce_ptr) = dist(rd);

        XLiveAPI::XSessionCreate(XNKIDtoUint64(&pSessionInfo->sessionID), data);

        pSessionInfo->hostAddress.inaOnline.s_addr =
            XLiveAPI::OnlineIP().sin_addr.s_addr;
        pSessionInfo->hostAddress.ina.s_addr =
            pSessionInfo->hostAddress.inaOnline.s_addr;

        memcpy(&pSessionInfo->hostAddress.abEnet, XLiveAPI::mac_address, 6);
        memcpy(&pSessionInfo->hostAddress.abOnline, XLiveAPI::mac_address, 6);
        pSessionInfo->hostAddress.wPortOnline = XLiveAPI::GetPlayerPort();
      } else {
        // Check if session is valid
        auto sessionId = XNKIDtoUint64(&pSessionInfo->sessionID);

        if (sessionId == 0) {
          assert_always();
          return X_E_SUCCESS;
        }

        auto session = XLiveAPI::XSessionGet(sessionId);

        pSessionInfo->hostAddress.inaOnline.s_addr =
            inet_addr(session.hostAddress.c_str());
        pSessionInfo->hostAddress.ina.s_addr =
            pSessionInfo->hostAddress.inaOnline.s_addr;

        memcpy(&pSessionInfo->hostAddress.abEnet, session.macAddress.c_str(),
               6);
        memcpy(&pSessionInfo->hostAddress.abOnline, session.macAddress.c_str(),
               6);
        pSessionInfo->hostAddress.wPortOnline = XLiveAPI::GetPlayerPort();
      }

      if (&pSessionInfo->sessionID) {
        XLiveAPI::sessionHandleMap.emplace(data->session_handle,
                                 XNKIDtoUint64(&pSessionInfo->sessionID));
      }

      XLiveAPI::clearXnaddrCache();

      return X_E_SUCCESS;
    }
    case 0x000B0011: {
      // TODO(PermaNull): reverse buffer contents.
      XELOGI("XGISessionDelete");

      struct SessionDelete {
        xe::be<uint32_t> session_handle;
      }* session = reinterpret_cast<SessionDelete*>(buffer);

      XLiveAPI::DeleteSession(XLiveAPI::sessionHandleMap[session->session_handle]);

      return X_STATUS_SUCCESS;
    }
    case 0x000B0012: {
      assert_true(buffer_length == 0x14);

      struct SessionJoin {
        xe::be<uint32_t> session_ptr;
        xe::be<uint32_t> array_count;
        xe::be<uint32_t> xuid_array;
        xe::be<uint32_t> user_index_array;
        xe::be<uint32_t> private_slots_array;
      }* data = reinterpret_cast<SessionJoin*>(buffer);

      // Local uses user indices, remote uses XUIDs
      if (data->xuid_array == 0) {
        XELOGI("XGISessionJoinLocal({:08X}, {}, {:08X}, {:08X}, {:08X})",
               data->session_ptr, data->array_count, data->xuid_array,
               data->user_index_array, data->private_slots_array);
      } else {
        XELOGI("XGISessionJoinRemote({:08X}, {}, {:08X}, {:08X}, {:08X})",
               data->session_ptr, data->array_count, data->xuid_array,
               data->user_index_array, data->private_slots_array);

        struct XSessionJoinRemote {
          xe::be<uint32_t> session_ptr;
          xe::be<uint32_t> array_count;
          xe::be<uint32_t> xuid_array;
          xe::be<uint32_t> private_slots_array;
          xe::be<uint32_t> overlapped;
        }* data = reinterpret_cast<XSessionJoinRemote*>(buffer);

        auto xuids =
            memory_->TranslateVirtual<xe::be<uint64_t>*>(data->xuid_array);

        Document doc;
        doc.SetObject();

        rapidjson::Value xuidsJsonArray(rapidjson::kArrayType);

        for (unsigned int i = 0; i < data->array_count; i++) {
          std::stringstream xuidSS;
          xuidSS << std::hex << std::noshowbase << std::setw(16)
                 << std::setfill('0') << xuids[i];

          rapidjson::Value value;
          value.SetString(xuidSS.str().c_str(), 16, doc.GetAllocator());
          xuidsJsonArray.PushBack(value, doc.GetAllocator());
        }

        doc.AddMember("xuids", xuidsJsonArray, doc.GetAllocator());

        rapidjson::StringBuffer buffer;
        PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        XLiveAPI::SessionJoinRemote(XLiveAPI::sessionHandleMap[data->session_ptr],
                                    buffer.GetString());
      }

      XLiveAPI::clearXnaddrCache();

      return X_E_SUCCESS;
    }
    case 0x000B0013: {
      assert_true(buffer_length == 0x14);

      struct XSessionLeaveRemote {
        xe::be<uint32_t> session_ptr;
        xe::be<uint32_t> array_count;
        xe::be<uint32_t> xuid_array;
        xe::be<uint32_t> user_index_array;
        xe::be<uint32_t> private_slots_array;
      }* data = reinterpret_cast<XSessionLeaveRemote*>(buffer);

      // Local uses user indices, remote uses XUIDs
      if (data->xuid_array == 0) {
        XELOGI("XGISessionLeaveLocal({:08X}, {}, {:08X}, {:08X}, {:08X})",
               data->session_ptr, data->array_count, data->xuid_array,
               data->user_index_array, data->private_slots_array);
      } else {
        XELOGI("XGISessionLeaveRemote({:08X}, {}, {:08X}, {:08X}, {:08X})",
               data->session_ptr, data->array_count, data->xuid_array,
               data->user_index_array, data->private_slots_array);

        auto xuids =
            memory_->TranslateVirtual<xe::be<uint64_t>*>(data->xuid_array);

        Document doc;
        doc.SetObject();

        rapidjson::Value xuidsJsonArray(rapidjson::kArrayType);

        for (unsigned int i = 0; i < data->array_count; i++) {
          std::stringstream xuidSS;
          xuidSS << std::hex << std::noshowbase << std::setw(16)
                 << std::setfill('0') << xuids[i];
          rapidjson::Value value;

          value.SetString(xuidSS.str().c_str(), 16, doc.GetAllocator());
          xuidsJsonArray.PushBack(value, doc.GetAllocator());
        }

        doc.AddMember("xuids", xuidsJsonArray, doc.GetAllocator());

        rapidjson::StringBuffer buffer;
        PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        XLiveAPI::SessionLeaveRemote(XLiveAPI::sessionHandleMap[data->session_ptr],
                                     buffer.GetString());
      }

      XLiveAPI::clearXnaddrCache();

      return X_E_SUCCESS;
    }
    case 0x000B0014: {
      // Gets 584107FB in game.
      // get high score table?

      XELOGI("XSessionStart");
      return X_STATUS_SUCCESS;
    }
    case 0x000B0015: {
      // send high scores?

      XELOGI("XSessionEnd");
      return X_STATUS_SUCCESS;
    }
    case 0x000B0025: {
      XELOGI("XSessionWriteStats");

      XLiveAPI::XSessionWriteStats* data =
          reinterpret_cast<XLiveAPI::XSessionWriteStats*>(buffer);

      if (XLiveAPI::sessionHandleMap[data->session_handle] == 0) {
        assert_always();
        return X_STATUS_SUCCESS;
      }

      auto leaderboard =
          memory_->TranslateVirtual<XLiveAPI::XSessionViewProperties*>(
              data->leaderboards_guest_address);

      XLiveAPI::SessionWriteStats(
          XLiveAPI::sessionHandleMap[data->session_handle], data, leaderboard);

      return X_STATUS_SUCCESS;
    }
    case 0x000B001B: {
      XELOGI("XSessionSearchByID unimplemented");
      return X_E_SUCCESS;
    }
    case 0x000B0065: {
      XELOGI("XSessionSearchWeighted unimplemented");
      return X_E_SUCCESS;
    }
    case 0x000B0026: {
      XELOGI("XSessionFlushStats unimplemented");
      return X_E_SUCCESS;
    }
    case 0x000B001F: {
      XELOGI("XSessionModifySkill unimplemented");
      return X_E_SUCCESS;
    }
    case 0x000B0019: {
      XELOGI("XSessionGetInvitationData unimplemented");
      return X_E_SUCCESS;
    }
    case 0x000B0041: {
      assert_true(!buffer_length || buffer_length == 32);
      // 00000000 2789fecc 00000000 00000000 200491e0 00000000 200491f0 20049340
      uint32_t user_index = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t context_ptr = xe::load_and_swap<uint32_t>(buffer + 16);
      auto context =
          context_ptr ? memory_->TranslateVirtual(context_ptr) : nullptr;
      uint32_t context_id =
          context ? xe::load_and_swap<uint32_t>(context + 0) : 0;
      XELOGD("XGIUserGetContext({:08X}, {:08X}{:08X}))", user_index,
             context_ptr, context_id);
      uint32_t value = 0;
      if (context) {
        xe::store_and_swap<uint32_t>(context + 4, value);
      }
      return X_E_FAIL;
    }
    case 0x000B0071: {
      XELOGD("XGI 0x000B0071, unimplemented");
      return X_E_SUCCESS;
    }
  }
  XELOGE(
      "Unimplemented XGI message app={:08X}, msg={:08X}, arg1={:08X}, "
      "arg2={:08X}",
      app_id(), message, buffer_ptr, buffer_length);
  return X_E_FAIL;
}
}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe