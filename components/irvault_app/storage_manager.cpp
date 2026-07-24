#include "storage_manager.h"

#include <cstddef>
#include <cstring>

#include "esp_heap_caps.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "factory_defaults.h"
#include "mitsubishi_codec.h"

namespace esphome::irvault {

static const char *const TAG = "irvault.storage";
static constexpr uint32_t RECORD_MAGIC = 0x49525632;
static constexpr uint16_t RECORD_VERSION = 2;
static constexpr uint32_t LEGACY_PREF_BASE = 0x49525610;
static constexpr uint16_t LEGACY_MAX_RAW_DURATIONS = 512;

/**
 * On-flash V2 header. The record is this header followed by pulse_count
 * int32_t durations. crc is calculated over bytes [0, crc) and the durations;
 * tail padding is deliberately excluded.
 */
struct StoredProfileHeaderV2 {
  uint64_t created_timestamp;
  uint64_t last_used_timestamp;
  uint32_t magic;
  uint32_t generation;
  uint32_t carrier_hz;
  uint32_t reserved;
  char display_name[8];
  uint16_t version;
  uint16_t header_size;
  uint16_t pulse_count;
  uint16_t flags;
  uint32_t crc;
};

static_assert(offsetof(StoredProfileHeaderV2, crc) == 48);
static_assert(sizeof(StoredProfileHeaderV2) == 56);

/** Exact fixed-size layout used before the long-frame storage migration. */
struct LegacyIRProfileV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t pulse_count;
  uint32_t generation;
  uint32_t carrier_hz;
  char display_name[8];
  uint64_t created_timestamp;
  uint64_t last_used_timestamp;
  int32_t raw[LEGACY_MAX_RAW_DURATIONS];
  uint32_t crc;
};

static_assert(sizeof(LegacyIRProfileV1) == 2096);

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length) {
  for (size_t index = 0; index < length; index++) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc;
}

static uint32_t record_crc(const StoredProfileHeaderV2 &header,
                           const int32_t *raw) {
  uint32_t crc = 0xFFFFFFFF;
  crc = crc32_update(
      crc, reinterpret_cast<const uint8_t *>(&header),
      offsetof(StoredProfileHeaderV2, crc));
  crc = crc32_update(crc, reinterpret_cast<const uint8_t *>(raw),
                     static_cast<size_t>(header.pulse_count) * sizeof(int32_t));
  return ~crc;
}

static uint32_t legacy_crc(const LegacyIRProfileV1 &profile) {
  uint32_t crc = 0xFFFFFFFF;
  crc = crc32_update(crc, reinterpret_cast<const uint8_t *>(&profile),
                     offsetof(LegacyIRProfileV1, crc));
  return ~crc;
}

static bool legacy_valid(const LegacyIRProfileV1 &profile) {
  return profile.magic == 0x49525631 && profile.version == 1 &&
         profile.pulse_count >= 20 &&
         profile.pulse_count <= LEGACY_MAX_RAW_DURATIONS &&
         profile.carrier_hz >= 20000 && profile.carrier_hz <= 60000 &&
         profile.crc == legacy_crc(profile);
}

const char *StorageManager::key_(uint8_t slot, uint8_t copy) {
  static const char *const keys[3][2] = {
      {"s0a", "s0b"},
      {"s1a", "s1b"},
      {"s2a", "s2b"},
  };
  return slot < 3 && copy < 2 ? keys[slot][copy] : "invalid";
}

bool StorageManager::read_record_(uint8_t slot, uint8_t copy,
                                  IRProfile *profile,
                                  uint32_t *generation) {
  if (slot >= 3 || copy >= 2 || profile == nullptr ||
      this->record_buffer_ == nullptr)
    return false;

  size_t length = 0;
  esp_err_t result =
      nvs_get_blob(this->nvs_handle_, key_(slot, copy), nullptr, &length);
  if (result != ESP_OK || length < sizeof(StoredProfileHeaderV2) ||
      length > this->record_capacity_)
    return false;

  result = nvs_get_blob(this->nvs_handle_, key_(slot, copy),
                        this->record_buffer_, &length);
  if (result != ESP_OK)
    return false;

  StoredProfileHeaderV2 header{};
  std::memcpy(&header, this->record_buffer_, sizeof(header));
  const size_t expected =
      sizeof(header) + static_cast<size_t>(header.pulse_count) * sizeof(int32_t);
  if (header.magic != RECORD_MAGIC || header.version != RECORD_VERSION ||
      header.header_size != sizeof(header) || header.pulse_count < 20 ||
      header.pulse_count > MAX_RAW_DURATIONS || header.carrier_hz < 20000 ||
      header.carrier_hz > 60000 || length != expected)
    return false;

  const auto *stored_raw = reinterpret_cast<const int32_t *>(
      this->record_buffer_ + sizeof(header));
  if (header.crc != record_crc(header, stored_raw))
    return false;

  reset_ir_profile(profile);
  profile->pulse_count = header.pulse_count;
  profile->generation = header.generation;
  profile->carrier_hz = header.carrier_hz;
  std::memcpy(profile->display_name, header.display_name,
              sizeof(profile->display_name));
  profile->created_timestamp = header.created_timestamp;
  profile->last_used_timestamp = header.last_used_timestamp;
  std::memcpy(profile->raw, stored_raw,
              static_cast<size_t>(header.pulse_count) * sizeof(int32_t));
  profile->crc = header.crc;
  if (generation != nullptr)
    *generation = header.generation;
  return true;
}

bool StorageManager::write_record_(uint8_t slot, uint8_t copy,
                                   const IRProfile &profile,
                                   uint32_t generation) {
  if (slot >= 3 || copy >= 2 || profile.pulse_count < 20 ||
      profile.pulse_count > MAX_RAW_DURATIONS ||
      this->record_buffer_ == nullptr)
    return false;

  StoredProfileHeaderV2 header{};
  header.created_timestamp = profile.created_timestamp;
  header.last_used_timestamp = profile.last_used_timestamp;
  header.magic = RECORD_MAGIC;
  header.generation = generation;
  header.carrier_hz =
      profile.carrier_hz == 0 ? DEFAULT_CARRIER_HZ : profile.carrier_hz;
  static const char *const labels[3] = {"Cool", "Heat", "Off"};
  std::strncpy(header.display_name, labels[slot],
               sizeof(header.display_name) - 1);
  header.version = RECORD_VERSION;
  header.header_size = sizeof(header);
  header.pulse_count = profile.pulse_count;
  header.flags = 0;
  header.crc = record_crc(header, profile.raw);

  const size_t length =
      sizeof(header) +
      static_cast<size_t>(profile.pulse_count) * sizeof(int32_t);
  std::memcpy(this->record_buffer_, &header, sizeof(header));
  std::memcpy(this->record_buffer_ + sizeof(header), profile.raw,
              static_cast<size_t>(profile.pulse_count) * sizeof(int32_t));

  esp_err_t result =
      nvs_set_blob(this->nvs_handle_, key_(slot, copy),
                   this->record_buffer_, length);
  if (result == ESP_OK)
    result = nvs_commit(this->nvs_handle_);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "[Storage] Slot %u V2 write failed: %s", slot + 1,
             esp_err_to_name(result));
    return false;
  }
  return true;
}

bool StorageManager::migrate_legacy_slot_(uint8_t slot) {
  auto *legacy = static_cast<LegacyIRProfileV1 *>(
      heap_caps_calloc(1, sizeof(LegacyIRProfileV1),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (legacy == nullptr)
    return false;

  bool found = false;
  uint32_t newest_generation = 0;
  reset_ir_profile(this->scratch_);
  for (uint8_t copy = 0; copy < 2; copy++) {
    ESPPreferenceObject preference =
        global_preferences->make_preference<LegacyIRProfileV1>(
            LEGACY_PREF_BASE + slot * 2 + copy);
    std::memset(legacy, 0, sizeof(*legacy));
    if (!preference.load(legacy) || !legacy_valid(*legacy))
      continue;
    if (found && legacy->generation <= newest_generation)
      continue;

    found = true;
    newest_generation = legacy->generation;
    reset_ir_profile(this->scratch_);
    this->scratch_->pulse_count = legacy->pulse_count;
    this->scratch_->generation = legacy->generation;
    this->scratch_->carrier_hz = legacy->carrier_hz;
    std::memcpy(this->scratch_->display_name, legacy->display_name,
                sizeof(this->scratch_->display_name));
    this->scratch_->created_timestamp = legacy->created_timestamp;
    this->scratch_->last_used_timestamp = legacy->last_used_timestamp;
    std::memcpy(this->scratch_->raw, legacy->raw,
                static_cast<size_t>(legacy->pulse_count) * sizeof(int32_t));
  }
  heap_caps_free(legacy);
  if (!found)
    return false;

  const uint8_t target = newest_generation & 1U;
  if (!this->write_record_(slot, target, *this->scratch_,
                           newest_generation))
    return false;
  this->active_copy_[slot] = target;
  this->generation_[slot] = newest_generation;
  this->learned_[slot] = true;
  ESP_LOGI(TAG,
           "[Storage] Slot %u migrated V1->V2 generation=%lu pulses=%u",
           slot + 1, static_cast<unsigned long>(newest_generation),
           this->scratch_->pulse_count);
  return true;
}

bool StorageManager::seed_factory_slot_(uint8_t slot) {
  const uint8_t *state = factory_default_mitsubishi_state(slot);
  if (state == nullptr || this->scratch_ == nullptr ||
      !validate_mitsubishi_state(state))
    return false;

  reset_ir_profile(this->scratch_);
  uint16_t pulse_count = 0;
  if (!encode_mitsubishi_state(
          state, this->scratch_->raw, MAX_RAW_DURATIONS, &pulse_count))
    return false;
  this->scratch_->pulse_count = pulse_count;
  this->scratch_->carrier_hz = DEFAULT_CARRIER_HZ;

  if (!this->save_slot(slot, *this->scratch_))
    return false;

  ESP_LOGI(TAG,
           "[Storage] Slot %u seeded from firmware factory default "
           "generation=%lu pulses=%u",
           slot + 1, static_cast<unsigned long>(this->generation_[slot]),
           pulse_count);
  return true;
}

bool StorageManager::setup() {
  this->scratch_ = allocate_ir_profile();
  this->record_capacity_ =
      sizeof(StoredProfileHeaderV2) +
      static_cast<size_t>(MAX_RAW_DURATIONS) * sizeof(int32_t);
  this->record_buffer_ = static_cast<uint8_t *>(
      heap_caps_malloc(this->record_capacity_,
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->scratch_ == nullptr || this->record_buffer_ == nullptr) {
    ESP_LOGE(TAG, "[Storage] PSRAM allocation failed");
    return false;
  }

  esp_err_t result = nvs_open("irvault", NVS_READWRITE, &this->nvs_handle_);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "[Storage] Could not open V2 NVS namespace: %s",
             esp_err_to_name(result));
    return false;
  }

  for (uint8_t slot = 0; slot < 3; slot++) {
    for (uint8_t copy = 0; copy < 2; copy++) {
      uint32_t generation = 0;
      if (!this->read_record_(slot, copy, this->scratch_, &generation))
        continue;
      if (!this->learned_[slot] || generation > this->generation_[slot]) {
        this->learned_[slot] = true;
        this->active_copy_[slot] = copy;
        this->generation_[slot] = generation;
      }
    }
    if (!this->learned_[slot] &&
        !this->migrate_legacy_slot_(slot) &&
        !this->seed_factory_slot_(slot)) {
      ESP_LOGE(TAG, "[Storage] Slot %u factory default initialization failed",
               slot + 1);
      return false;
    }
    ESP_LOGI(TAG, "[Storage] Slot %u: %s V2 generation=%lu", slot + 1,
             this->learned_[slot] ? "Learned" : "Empty",
             static_cast<unsigned long>(this->generation_[slot]));
  }
  ESP_LOGI(TAG,
           "[Storage] Variable RAW records ready max=%u pulses "
           "record_capacity=%u bytes",
           MAX_RAW_DURATIONS, static_cast<unsigned>(this->record_capacity_));
  return true;
}

bool StorageManager::is_learned(uint8_t slot) const {
  return slot < 3 && this->learned_[slot];
}

bool StorageManager::load_slot(uint8_t slot, IRProfile *profile) {
  if (slot >= 3 || profile == nullptr || !this->learned_[slot])
    return false;
  uint32_t generation = 0;
  if (!this->read_record_(slot, this->active_copy_[slot], profile,
                          &generation) ||
      generation != this->generation_[slot]) {
    ESP_LOGE(TAG, "[Storage] Slot %u active V2 record failed validation",
             slot + 1);
    return false;
  }
  return true;
}

bool StorageManager::save_slot(uint8_t slot, const IRProfile &source) {
  if (slot >= 3 || source.pulse_count < 20 ||
      source.pulse_count > MAX_RAW_DURATIONS)
    return false;

  const uint32_t next_generation =
      this->learned_[slot] ? this->generation_[slot] + 1 : 1;
  const uint8_t target = next_generation & 1U;
  if (!this->write_record_(slot, target, source, next_generation))
    return false;

  uint32_t verified_generation = 0;
  if (!this->read_record_(slot, target, this->scratch_,
                          &verified_generation) ||
      verified_generation != next_generation) {
    ESP_LOGE(TAG, "[Storage] Slot %u V2 verification failed", slot + 1);
    return false;
  }

  this->active_copy_[slot] = target;
  this->generation_[slot] = next_generation;
  this->learned_[slot] = true;
  ESP_LOGI(TAG,
           "[Storage] Slot %u saved V2 generation=%lu pulses=%u "
           "blob=%u bytes",
           slot + 1, static_cast<unsigned long>(next_generation),
           source.pulse_count,
           static_cast<unsigned>(sizeof(StoredProfileHeaderV2) +
                                 static_cast<size_t>(source.pulse_count) *
                                     sizeof(int32_t)));
  return true;
}

}  // namespace esphome::irvault
