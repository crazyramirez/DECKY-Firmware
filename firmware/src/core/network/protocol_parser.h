#pragma once
// Protocol v1 (PC-Control-Deck-Agent docs/PROTOCOL.md) decoding on the deck. Pure functions over ArduinoJson so they can
// be tested on target against the shared fixtures.

#include <stddef.h>
#include <stdint.h>

#include "crypto/models.h"
#include "telemetry/models.h"

namespace proto {

constexpr int kVersion = 1;
constexpr size_t kMaxFrame = 16 * 1024;

enum class MsgType : uint8_t {
  Unknown, Capabilities, Snapshot, Heartbeat, Error, CommandResult, PairResult, AuthResult, WeatherData, WeatherResults,
  CryptoData, CryptoCandles
};

struct AuthInfo {
  bool ok = false;
  char token[72] = "";      // pair.result only; NEVER log it
  char agent_id[24] = "";
  char agent_name[40] = "";
  char error[24] = "";
};

enum class ParseResult : uint8_t { Ok, TooLarge, Malformed, UnsupportedVersion, UnknownType };

struct Capabilities {
  bool volume = false, media = false, seek = false, lock = false, power = false;
  bool auth_required = false;
  bool authenticated = true;  // false: the agent wants auth.request / pair.request first
  char provider[32] = "";
  char first_note[96] = "";
  uint8_t note_count = 0;
};

struct CommandResultInfo {
  uint32_t request_id = 0;  // parsed from "<boot>-<n>"; 0 if it is not ours
  bool ok = false;
  int32_t applied_value = -1;
  char action[24] = "";
  char error[24] = "";
};

struct ErrorInfo {
  char code[24] = "";
  char message[64] = "";
};

struct PlaceResults {
  char query[64] = "";
  bool error = false;  // the agent could not reach the geocoding service
  telemetry::Place places[telemetry::kMaxPlaces];
  uint8_t count = 0;
};

struct Message {
  MsgType type = MsgType::Unknown;
  telemetry::Snapshot snapshot;  // valid when type == Snapshot
  Capabilities caps;             // valid when type == Capabilities
  ErrorInfo error;               // valid when type == Error
  AuthInfo auth;                 // valid when type == PairResult / AuthResult
  CommandResultInfo cmd;         // valid when type == CommandResult
  telemetry::Weather weather;    // valid when type == WeatherData
  PlaceResults places;           // valid when type == WeatherResults
  // Crypto payloads are large: they are parsed into caller-owned buffers (PSRAM). When a pointer
  // is null the message type is still reported but the payload is skipped.
  crypto::Market *market = nullptr;  // filled when type == CryptoData
  crypto::Chart *chart = nullptr;    // filled when type == CryptoCandles
};

// Request ids are "<boot nonce hex>-<counter>" so a rebooted deck never collides with ids the
// agent already saw (the agent de-duplicates by id).
void set_boot_nonce(uint32_t nonce);
size_t build_command(char *buf, size_t n, uint32_t request_id, const char *action, int32_t value);

// Parses one frame. JSON null / missing numbers become NaN (never 0).
ParseResult parse(const char *json, size_t len, Message &out);
const char *result_name(ParseResult r);

// Builds the hello message into buf. Returns bytes written (0 on overflow).
size_t build_hello(char *buf, size_t n, const char *client_id, const char *fw);
size_t build_heartbeat(char *buf, size_t n);
size_t build_art_request(char *buf, size_t n, uint32_t art_id);  // ask the agent to resend a cover
size_t build_weather_search(char *buf, size_t n, const char *query, const char *lang);  // city search
size_t build_weather_request(char *buf, size_t n, float lat, float lon);                // forecast
size_t build_pair_request(char *buf, size_t n, const char *code, const char *name);
// Crypto (agent >= 0.6): pairs[i] / ids[i] = Binance pair and CoinGecko id of each watched coin.
size_t build_crypto_request(char *buf, size_t n, const char *const *pairs, const char *const *ids, int count,
                            const char *cur);
size_t build_crypto_chart(char *buf, size_t n, const char *pair, uint8_t range, const char *cur);
size_t build_auth_request(char *buf, size_t n, const char *token);

}  // namespace proto
