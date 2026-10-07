#include "state_store.h"

#include <Preferences.h>

#include "../util/crc32.h"

namespace state_store {

static const char* kStateNs = "state";
static const char* kCacheNs = "cache";
static const char* kBlobKeys[SRC_COUNT] = {"weather", "outage", "backup", "fx", "soc"};

struct BlobHeader {
  uint8_t version;
  uint32_t crc;
} __attribute__((packed));

void load(PersistedState& st) {
  Preferences p;
  p.begin(kStateNs, true);
  st.lastSuccessEpoch[SRC_WEATHER] = p.getUInt("ok_weather", 0);
  st.lastSuccessEpoch[SRC_OUTAGE] = p.getUInt("ok_outage", 0);
  st.lastSuccessEpoch[SRC_BACKUP] = p.getUInt("ok_backup", 0);
  st.lastSuccessEpoch[SRC_FX] = p.getUInt("ok_fx", 0);
  st.lastSuccessEpoch[SRC_SOC] = p.getUInt("ok_soc", 0);
  st.lastSntpEpoch = p.getUInt("sntp_last", 0);
  st.bootCount = p.getUInt("boot_count", 0);
  st.consecWifiFails = p.getUShort("wifi_fails", 0);
  st.lastOnlineEpoch = p.getUInt("online_last", 0);
  st.lastInternetEpoch = p.getUInt("inet_last", 0);
  st.lastOtaCheckEpoch = p.getUInt("ota_last", 0);
  st.lastFullEpoch = p.getUInt("full_last", 0);
  st.lastPage = p.getUChar("last_page", 0);
  // isKey first: a missing string key logs an [E] line (new keys in 0.3.0).
  st.otaTriedVersion = p.isKey("ota_tried") ? p.getString("ota_tried", "") : "";
  st.otaBadVersion = p.isKey("ota_bad") ? p.getString("ota_bad", "") : "";
  st.deyeToken = p.getString("deye_token", "");
  st.deyeTokenExpEpoch = p.getUInt("deye_tok_exp", 0);
  p.end();
}

void save(const PersistedState& st) {
  Preferences p;
  p.begin(kStateNs, false);
  p.putUInt("ok_weather", st.lastSuccessEpoch[SRC_WEATHER]);
  p.putUInt("ok_outage", st.lastSuccessEpoch[SRC_OUTAGE]);
  p.putUInt("ok_backup", st.lastSuccessEpoch[SRC_BACKUP]);
  p.putUInt("ok_fx", st.lastSuccessEpoch[SRC_FX]);
  p.putUInt("ok_soc", st.lastSuccessEpoch[SRC_SOC]);
  p.putUInt("sntp_last", st.lastSntpEpoch);
  p.putUInt("boot_count", st.bootCount);
  p.putUShort("wifi_fails", st.consecWifiFails);
  p.putUInt("online_last", st.lastOnlineEpoch);
  p.putUInt("inet_last", st.lastInternetEpoch);
  p.putUInt("ota_last", st.lastOtaCheckEpoch);
  p.putUInt("full_last", st.lastFullEpoch);
  p.putUChar("last_page", st.lastPage);
  p.putString("ota_tried", st.otaTriedVersion);
  p.putString("ota_bad", st.otaBadVersion);
  p.putString("deye_token", st.deyeToken);
  p.putUInt("deye_tok_exp", st.deyeTokenExpEpoch);
  p.end();
}

bool loadBlob(Source src, void* out, size_t size) {
  Preferences p;
  p.begin(kCacheNs, true);
  size_t stored = p.isKey(kBlobKeys[src]) ? p.getBytesLength(kBlobKeys[src]) : 0;
  if (stored != sizeof(BlobHeader) + size) {
    p.end();
    return false;
  }
  uint8_t* buf = static_cast<uint8_t*>(malloc(stored));
  if (!buf) {
    p.end();
    return false;
  }
  p.getBytes(kBlobKeys[src], buf, stored);
  p.end();

  BlobHeader hdr;
  memcpy(&hdr, buf, sizeof(hdr));
  bool ok = hdr.version == dash::kCacheVersion &&
            hdr.crc == crc32_calc(buf + sizeof(hdr), size);
  if (ok) memcpy(out, buf + sizeof(hdr), size);
  free(buf);
  return ok;
}

void saveBlob(Source src, const void* data, size_t size) {
  BlobHeader hdr;
  hdr.version = dash::kCacheVersion;
  hdr.crc = crc32_calc(data, size);

  Preferences p;
  p.begin(kCacheNs, false);

  uint8_t* buf = static_cast<uint8_t*>(malloc(sizeof(hdr) + size));
  if (!buf) {
    p.end();
    return;
  }
  memcpy(buf, &hdr, sizeof(hdr));
  memcpy(buf + sizeof(hdr), data, size);

  // Skip the erase+write cycle when content is unchanged. getBytes() refuses
  // a buffer shorter than the stored blob, so read the whole thing.
  size_t stored = p.isKey(kBlobKeys[src]) ? p.getBytesLength(kBlobKeys[src]) : 0;
  if (stored == sizeof(hdr) + size) {
    uint8_t* old = static_cast<uint8_t*>(malloc(stored));
    bool same = old && p.getBytes(kBlobKeys[src], old, stored) == stored &&
                memcmp(old, buf, stored) == 0;
    free(old);
    if (same) {
      free(buf);
      p.end();
      return;
    }
  }

  p.putBytes(kBlobKeys[src], buf, sizeof(hdr) + size);
  free(buf);
  p.end();
}

}  // namespace state_store
