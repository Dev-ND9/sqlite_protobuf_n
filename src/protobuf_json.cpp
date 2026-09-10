#include "protobuf_json.h"
#include "sqlite3ext.h"

#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <regex>

#include "protodec.h"

namespace sqlite_protobuf
{
    SQLITE_EXTENSION_INIT3

    namespace
    {
        /// Converts a binary blob of protobuf bytes to a JSON representation of the message,
        /// while automatically translating Manager.io mixed-endian GUID structures.
        void protobuf_to_json(sqlite3_context *context, int argc, sqlite3_value **argv)
        {
            if(argc < 1 || argc > 2)
            {
                sqlite3_result_error(context, "Wrong number of arguments", -1);
                return;
            } 

            // Load arguments
            sqlite3_value *data = argv[0];
            int64_t mode = argc > 1 ? sqlite3_value_int64(argv[1]) : 0;

            const uint8_t *raw_bytes = static_cast<const uint8_t *>(sqlite3_value_blob(data));
            size_t raw_len = static_cast<size_t>(sqlite3_value_bytes(data));

            // Manager.io GUIDs are embedded as 18-byte packed fields: [0x09] [8 bytes] [0x11] [8 bytes]
            // We can check the raw buffer directly to extract and format them if found.
            std::string forced_guid = "";
            if (raw_len == 18 && raw_bytes[0] == 0x09 && raw_bytes[10] == 0x11) {
                const uint8_t* p1 = &raw_bytes[1];
                const uint8_t* p2 = &raw_bytes[11];
                uint8_t b[16];
                
                // Little-endian swap for first 8 bytes
                for (int i = 0; i < 8; ++i) {
                    b[7 - i] = p1[i];
                }
                // Copy last 8 bytes as-is
                for (int i = 0; i < 8; ++i) {
                    b[8 + i] = p2[i];
                }

                // Format to standard 8-4-4-4-12 UUID string
                std::ostringstream ss;
                ss << std::hex << std::setfill('0');
                for (int i = 0; i < 16; ++i) {
                    ss << std::setw(2) << (int)b[i];
                    if (i == 3 || i == 5 || i == 7 || i == 9) ss << "-";
                }
                forced_guid = "\"" + ss.str() + "\"";
            }

            if (!forced_guid.empty()) {
                sqlite3_result_text(context, forced_guid.c_str(), forced_guid.length(), SQLITE_TRANSIENT);
                return;
            }

            // Standard decode for larger protobuf messages
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
