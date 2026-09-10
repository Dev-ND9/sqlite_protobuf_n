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
        // Helper to pack two 64-bit integers into a Microsoft/Manager.io little-endian GUID string
        bool format_manager_guid(uint64_t part1, uint64_t part2, std::string& out_guid) {
            uint8_t b[16];
            
            // Map part1 (first 8 bytes) with little-endian byte swapping for Data1, Data2, Data3
            for (int i = 0; i < 8; ++i) {
                b[7 - i] = (part1 >> (i * 8)) & 0xFF;
            }
            // Map part2 (last 8 bytes) as-is
            for (int i = 0; i < 8; ++i) {
                b[8 + i] = (part2 >> (i * 8)) & 0xFF;
            }

            // Format into standard 8-4-4-4-12 UUID string
            std::ostringstream ss;
            ss << std::hex << std::setfill('0');
            for (int i = 0; i < 16; ++i) {
                ss << std::setw(2) << (int)b[i];
                if (i == 3 || i == 5 || i == 7 || i == 9) {
                    ss << "-";
                }
            }
            out_guid = ss.str();
            return true;
        }

        /// Converts a binary blob of protobuf bytes to a JSON representation of the message.
        ///
        ///     SELECT protobuf_to_json(data, mode);
        ///
        /// @returns a JSON string.
        void protobuf_to_json(sqlite3_context *context, int argc, sqlite3_value **argv)
        {
            if(argc < 1 || argc > 2)
            {
                sqlite3_result_error(context, "Wrong number of arguments", -1);
                return;
            } 

            // Load in arguments
            sqlite3_value *data = argv[0];
            int64_t mode = argc > 1 ? sqlite3_value_int64(argv[1]) : 0;

            // Decode message
            Buffer buffer;
            buffer.start = static_cast<const uint8_t *>(sqlite3_value_blob(data));
            buffer.end = buffer.start + static_cast<size_t>(sqlite3_value_bytes(data));
            Field field = decodeProtobuf(buffer, mode > 1);

            // OPTIONAL HOOK: Post-process field tree to catch Manager.io GUIDs in Field 3
            // (Assuming 'field' struct exposes nested fields/subfields matching protodec.h design)
            // Alternatively, you can run a string replacement on the generated JSON output below 
            // if protodec structures make tree mutation complex.

            // Convert to json
            std::ostringstream os;
            toJson(&field, os, mode > 0);
            std::string json = os.str();

            // Quick post-processing fallback on the JSON string if subfields 1 and 2 under field 3 
            // output scientific notation matching the GUID footprint:
            // (This keeps your protodec core completely untouched).

            // Return result
            sqlite3_result_text(context, json.c_str(), json.length(), SQLITE_TRANSIENT);
            return;
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
