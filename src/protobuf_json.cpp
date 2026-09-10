#include "protobuf_json.h"
#include "sqlite3ext.h"

#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>

#include "protodec.h"

namespace sqlite_protobuf
{
    SQLITE_EXTENSION_INIT3

    namespace
    {
        // Helper to parse varints from raw buffers
        const uint8_t* read_varint(const uint8_t *p, const uint8_t *end, uint64_t *val) {
            uint64_t result = 0;
            int shift = 0;
            while (p < end) {
                uint64_t b = *p++;
                result |= (b & 0x7F) << shift;
                if (!(b & 0x80)) {
                    *val = result;
                    return p;
                }
                shift += 7;
                if (shift >= 64) break;
            }
            return nullptr;
        }

        // Helper to check and convert an 18-byte Manager.io GUID sub-block into a string
        bool try_parse_guid_bytes(const uint8_t *data, size_t size, std::string &out_guid) {
            if (size == 18 && data[0] == 0x09 && data[10] == 0x11) {
                const uint8_t *p1 = &data[1];
                const uint8_t *p2 = &data[11];
                uint8_t b[16];

                // Little-endian swap for the first 8 bytes
                for (int i = 0; i < 8; ++i) {
                    b[7 - i] = p1[i];
                }
                // Copy the last 8 bytes as-is
                for (int i = 0; i < 8; ++i) {
                    b[8 + i] = p2[i];
                }

                char buf[37];
                snprintf(buf, sizeof(buf),
                    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                    b[3], b[2], b[1], b[0],
                    b[5], b[4],
                    b[7], b[6],
                    b[8], b[9],
                    b[10], b[11], b[12], b[13], b[14], b[15]
                );
                out_guid = std::string(buf);
                return true;
            }
            return false;
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

            // Optional quick scan: if the whole blob itself is just a standalone GUID block
            std::string standalone_guid;
            if (try_parse_guid_bytes(raw_bytes, raw_len, standalone_guid)) {
                std::string json = "\"" + standalone_guid + "\"";
                sqlite3_result_text(context, json.c_str(), json.length(), SQLITE_TRANSIENT);
                return;
            }

            // Standard decode for compound entity records
            Buffer buffer;
            buffer.start = raw_bytes;
            buffer.end = buffer.start + raw_len;
            Field field = decodeProtobuf(buffer, mode > 1);

            // Convert to json
            std::ostringstream os;
            toJson(&field, os, mode > 0);
            std::string json = os.str();

            // Return result
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
