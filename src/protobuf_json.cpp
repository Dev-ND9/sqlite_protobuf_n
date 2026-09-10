#include "protobuf_json.h"
#include "sqlite3ext.h"

#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <vector>
#include <regex>

#include "protodec.h"

namespace sqlite_protobuf
{
    SQLITE_EXTENSION_INIT3

    namespace
    {
        // Helper to convert raw 16-byte Manager.io GUID bytes into a standard string
        std::string bytes_to_guid_str(const uint8_t *b) {
            char buf[37];
            snprintf(buf, sizeof(buf),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                b[3], b[2], b[1], b[0],
                b[5], b[4],
                b[7], b[6],
                b[8], b[9],
                b[10], b[11], b[12], b[13], b[14], b[15]
            );
            return std::string(buf);
        }

        // Helper to scan a raw buffer for Manager.io GUID signatures [0x09][8 bytes][0x11][8 bytes]
        std::vector<std::string> extract_manager_guids(const uint8_t *data, size_t size) {
            std::vector<std::string> guids;
            if (size < 18) return guids;

            for (size_t i = 0; i <= size - 18; ++i) {
                if (data[i] == 0x09 && data[i + 10] == 0x11) {
                    const uint8_t *p1 = &data[i + 1];  
                    const uint8_t *p2 = &data[i + 11]; 
                    uint8_t b[16];

                    for (int j = 0; j < 8; ++j) {
                        b[7 - j] = p1[j];
                    }
                    for (int j = 0; j < 8; ++j) {
                        b[8 + j] = p2[j];
                    }

                    guids.push_back(bytes_to_guid_str(b));
                    i += 17; 
                }
            }
            return guids;
        }

        void protobuf_to_json(sqlite3_context *context, int argc, sqlite3_value **argv)
        {
            if(argc < 1 || argc > 2)
            {
                sqlite3_result_error(context, "Wrong number of arguments", -1);
                return;
            } 

            sqlite3_value *data = argv[0];
            int64_t mode = argc > 1 ? sqlite3_value_int64(argv[1]) : 0;

            const uint8_t *raw_bytes = static_cast<const uint8_t *>(sqlite3_value_blob(data));
            size_t raw_len = static_cast<size_t>(sqlite3_value_bytes(data));

            if (!raw_bytes || raw_len == 0) {
                sqlite3_result_null(context);
                return;
            }

            // Extract GUIDs from raw bytes first
            std::vector<std::string> found_guids = extract_manager_guids(raw_bytes, raw_len);

            // If the whole blob is just a standalone GUID block
            if (raw_len == 18 && !found_guids.empty()) {
                std::string json = "\"" + found_guids[0] + "\"";
                sqlite3_result_text(context, json.c_str(), json.length(), SQLITE_TRANSIENT);
                return;
            }

            // Standard protobuf decode
            Buffer buffer;
            buffer.start = raw_bytes;
            buffer.end = buffer.start + raw_len;
            Field field = decodeProtobuf(buffer, mode > 1);

            // Generate standard JSON
            std::ostringstream os;
            toJson(&field, os, mode > 0);
            std::string json = os.str();

            // If we found Manager GUIDs, substitute their messy object representations in the JSON string
            // Manager.io protobuf encodes these as nested fields {"1":<num>,"2":<num>} or similar structures.
            // We can cleanly replace matching patterns or inject them. 
            // For a robust fallback, if there's a pattern of fields containing the GUID parts, we patch them.
            // Alternatively, if protodec outputs them as nested objects, let's substitute them sequentially:
            for (const auto& guid : found_guids) {
                // Look for typical protobuf subfield object structures representing the split 64-bit chunks
                // and replace them directly with the clean GUID string value.
                // Regex pattern to catch nested objects containing fields 1 and 2 with large numbers/scientific notation
                std::string replacement = "\"" + guid + "\"";
                
                // Simple heuristic replacement of the subfield object block if found in JSON
                // e.g., {"1":<num>,"2":<num>} or {"2":<num>,"1":<num>}
                json = std::regex_replace(json, std::regex(R"(\{\s*"1"\s*:\s*[^,\}]+\s*,\s*"2"\s*:\s*[^,\}]+\s*\})"), replacement);
                json = std::regex_replace(json, std::regex(R"(\{\s*"2"\s*:\s*[^,\}]+\s*,\s*"1"\s*:\s*[^,\}]+\s*\})"), replacement);
            }

            sqlite3_result_text(context, json.c_str(), json.length(), SQLITE_TRANSIENT);
        }

        void protobuf_of_json(sqlite3_context *context, int argc, sqlite3_value **argv)
        {
            sqlite3_result_error(context, "Not implemented", -1);
            return;
        }

    } // namespace

    int register_protobuf_json(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
    {
        int rc;
        rc = sqlite3_create_function(db, "protobuf_to_json", -1,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     nullptr, protobuf_to_json, nullptr, nullptr);
        if (rc != SQLITE_OK)
            return rc;

        return sqlite3_create_function(db, "protobuf_of_json", 1,
                                       SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                       nullptr, protobuf_of_json, nullptr, nullptr);
    }
} // namespace sqlite_protobuf
