/*
 * positron_db.c - bounded SQLite wrapper and row-level sync state.
 *
 * C89 implementation.  SQLite is compiled into this DLL from the pinned
 * amalgamation under third_party/sqlite.  Network I/O is intentionally not
 * present here; the host owns HTTP workers and calls the sync serialization
 * entry points.
 */

#include <windows.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "sqlite3.h"
#include "positron_json.h"
#include "positron_db.h"

#define PDB_MAX_ERROR       256
#define PDB_MAX_META        128
#define PDB_MAX_ROW_BYTES   PDB_SYNC_MAX_BODY_BYTES
#define PDB_SYNC_PULL_LIMIT 64

#define PDB_DIRTY_UPSERT 1
#define PDB_DIRTY_DELETE 2

typedef struct PDbTableInfo {
    int used;
    char table_name[PDB_SYNC_NAME_MAX];
    char primary_key[PDB_SYNC_NAME_MAX];
    int column_count;
    char columns[PDB_SYNC_MAX_COLUMNS][PDB_SYNC_NAME_MAX];
    int column_types[PDB_SYNC_MAX_COLUMNS];
} PDbTableInfo;

struct PDb {
    sqlite3* sqlite;
    int mode;
    int internal;
    int migration;
    int applying;
    int cancelled;
    int in_transaction;
    char last_error[PDB_MAX_ERROR];
    PDbTableInfo tables[PDB_SYNC_MAX_TABLES];
    int table_count;
    PDbStmtHandle statements;
};

struct PDbStmt {
    PDbHandle owner;
    sqlite3_stmt* sqlite_stmt;
    int complete;
    PDbStmtHandle next;
};

typedef struct PDbWriter {
    char* buffer;
    int capacity;
    int length;
    int overflow;
} PDbWriter;

static int pdb_drain_dirty(PDbHandle db);
static int pdb_delete_dirty(PDbHandle db, const char* table_name,
        const char* row_key);

static void pdb_copy_text(char* destination, int capacity,
        const char* source)
{
    int length;

    if (destination == NULL || capacity <= 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    length = (int)strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
    }
    if (length > 0) {
        memcpy(destination, source, (size_t)length);
    }
    destination[length] = '\0';
}

static void pdb_set_error(PDbHandle db, const char* message)
{
    if (db == NULL) {
        return;
    }
    pdb_copy_text(db->last_error, sizeof(db->last_error), message);
}

static int pdb_set_sqlite_error(PDbHandle db, int sqlite_rc)
{
    const char* message;

    if (db != NULL && db->sqlite != NULL) {
        message = sqlite3_errmsg(db->sqlite);
        pdb_set_error(db, message);
    }
    if (sqlite_rc == SQLITE_BUSY || sqlite_rc == SQLITE_LOCKED) {
        return PDB_BUSY;
    }
    if (sqlite_rc == SQLITE_NOMEM) {
        return PDB_NOMEM;
    }
    if (sqlite_rc == SQLITE_INTERRUPT) {
        return PDB_STATE;
    }
    return PDB_ERROR;
}

static int pdb_is_identifier(const char* value)
{
    int index;
    unsigned char ch;

    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    if ((int)strlen(value) >= PDB_SYNC_NAME_MAX) {
        return 0;
    }
    ch = (unsigned char)value[0];
    if (!(isalpha(ch) || ch == '_')) {
        return 0;
    }
    for (index = 1; value[index] != '\0'; ++index) {
        ch = (unsigned char)value[index];
        if (!(isalnum(ch) || ch == '_')) {
            return 0;
        }
    }
    return 1;
}

static int pdb_is_reserved_name(const char* value)
{
    return value != NULL && strncmp(value, "__pdb_", 6) == 0;
}

static int pdb_quote_identifier(char* output, int capacity,
        const char* value)
{
    int length;

    if (output == NULL || capacity <= 0 || !pdb_is_identifier(value)) {
        return 0;
    }
    length = (int)strlen(value);
    if (length + 3 > capacity) {
        return 0;
    }
    output[0] = '"';
    memcpy(output + 1, value, (size_t)length);
    output[length + 1] = '"';
    output[length + 2] = '\0';
    return length + 2;
}

static int pdb_json_hex(unsigned char value)
{
    static const char hex[] = "0123456789ABCDEF";
    return hex[value & 0x0F];
}

static void pdb_writer_init(PDbWriter* writer, char* buffer, int capacity)
{
    writer->buffer = buffer;
    writer->capacity = capacity;
    writer->length = 0;
    writer->overflow = 0;
    if (buffer != NULL && capacity > 0) {
        buffer[0] = '\0';
    }
}

static void pdb_writer_put(PDbWriter* writer, const char* text, int length)
{
    int writable;

    if (writer == NULL || text == NULL || length < 0) {
        return;
    }
    if (writer->length > PDB_SYNC_MAX_BODY_BYTES - length) {
        writer->overflow = 1;
        return;
    }
    if (writer->buffer == NULL || writer->capacity <= 0) {
        writer->length += length;
        return;
    }
    writable = writer->capacity - 1 - writer->length;
    if (writable < length) {
        writer->overflow = 1;
        if (writable > 0) {
            memcpy(writer->buffer + writer->length, text, (size_t)writable);
            writer->length += writable;
            writer->buffer[writer->length] = '\0';
        }
        return;
    }
    if (length > 0) {
        memcpy(writer->buffer + writer->length, text, (size_t)length);
    }
    writer->length += length;
    writer->buffer[writer->length] = '\0';
}

static void pdb_writer_literal(PDbWriter* writer, const char* text)
{
    if (text != NULL) {
        pdb_writer_put(writer, text, (int)strlen(text));
    }
}

static void pdb_writer_char(PDbWriter* writer, char value)
{
    pdb_writer_put(writer, &value, 1);
}

static void pdb_writer_json_string(PDbWriter* writer, const char* text)
{
    const unsigned char* cursor;
    unsigned char value;
    char escaped[6];

    pdb_writer_char(writer, '"');
    if (text != NULL) {
        cursor = (const unsigned char*)text;
        while (*cursor != 0) {
            value = *cursor++;
            if (value == '"' || value == '\\') {
                escaped[0] = '\\';
                escaped[1] = (char)value;
                pdb_writer_put(writer, escaped, 2);
            } else if (value == '\b' || value == '\f' ||
                    value == '\n' || value == '\r' || value == '\t') {
                escaped[0] = '\\';
                escaped[1] = value == '\b' ? 'b' :
                        value == '\f' ? 'f' :
                        value == '\n' ? 'n' :
                        value == '\r' ? 'r' : 't';
                pdb_writer_put(writer, escaped, 2);
            } else if (value < 0x20) {
                escaped[0] = '\\';
                escaped[1] = 'u';
                escaped[2] = '0';
                escaped[3] = '0';
                escaped[4] = (char)pdb_json_hex((unsigned char)(value >> 4));
                escaped[5] = (char)pdb_json_hex(value);
                pdb_writer_put(writer, escaped, 6);
            } else {
                pdb_writer_char(writer, (char)value);
            }
        }
    }
    pdb_writer_char(writer, '"');
}

static void pdb_writer_int64(PDbWriter* writer, __int64 value)
{
    char text[32];
    int length;

    length = _snprintf(text, sizeof(text) - 1, "%I64d", value);
    if (length < 0) {
        text[sizeof(text) - 1] = '\0';
        length = (int)strlen(text);
    } else {
        text[length] = '\0';
    }
    pdb_writer_put(writer, text, length);
}

static void pdb_writer_double(PDbWriter* writer, double value)
{
    char text[64];
    int length;

    length = _snprintf(text, sizeof(text) - 1, "%.17g", value);
    if (length < 0) {
        text[sizeof(text) - 1] = '\0';
        length = (int)strlen(text);
    } else {
        text[length] = '\0';
    }
    pdb_writer_put(writer, text, length);
}

static const char pdb_base64_alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int pdb_base64_encoded_size(int length)
{
    if (length < 0 || length > (PDB_SYNC_MAX_BODY_BYTES / 4) * 3) {
        return -1;
    }
    return ((length + 2) / 3) * 4;
}

static int pdb_base64_encode(PDbWriter* writer, const unsigned char* data,
        int length)
{
    int index;
    int remain;
    unsigned int value;
    char output[4];

    if (length < 0 || (length > 0 && data == NULL)) {
        return 0;
    }
    for (index = 0; index < length; index += 3) {
        remain = length - index;
        value = ((unsigned int)data[index]) << 16;
        if (remain > 1) {
            value |= ((unsigned int)data[index + 1]) << 8;
        }
        if (remain > 2) {
            value |= (unsigned int)data[index + 2];
        }
        output[0] = pdb_base64_alphabet[(value >> 18) & 63];
        output[1] = pdb_base64_alphabet[(value >> 12) & 63];
        output[2] = remain > 1 ? pdb_base64_alphabet[(value >> 6) & 63] : '=';
        output[3] = remain > 2 ? pdb_base64_alphabet[value & 63] : '=';
        pdb_writer_put(writer, output, 4);
    }
    return writer->overflow == 0;
}

static int pdb_base64_value(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }
    if (value == '+') {
        return 62;
    }
    if (value == '/') {
        return 63;
    }
    return -1;
}

static int pdb_base64_decode(const char* text, unsigned char* output,
        int capacity, int* outLength)
{
    int length;
    int index;
    int value0;
    int value1;
    int value2;
    int value3;
    int produced;

    if (text == NULL || output == NULL || outLength == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    length = (int)strlen(text);
    if ((length % 4) != 0) {
        return PDB_ERROR;
    }
    if (length == 0) {
        *outLength = 0;
        return PDB_OK;
    }
    produced = 0;
    for (index = 0; index < length; index += 4) {
        value0 = pdb_base64_value(text[index]);
        value1 = pdb_base64_value(text[index + 1]);
        value2 = text[index + 2] == '=' ? -2 : pdb_base64_value(text[index + 2]);
        value3 = text[index + 3] == '=' ? -2 : pdb_base64_value(text[index + 3]);
        if (value0 < 0 || value1 < 0 || value2 == -1 || value3 == -1) {
            return PDB_ERROR;
        }
        if ((!((index + 4) == length) &&
                (value2 == -2 || value3 == -2)) ||
                (value2 == -2 && value3 != -2)) {
            return PDB_ERROR;
        }
        if (produced + 1 > capacity) {
            return PDB_BUFFER_TOO_SMALL;
        }
        output[produced++] = (unsigned char)((value0 << 2) | (value1 >> 4));
        if (value2 != -2) {
            if (produced + 1 > capacity) {
                return PDB_BUFFER_TOO_SMALL;
            }
            output[produced++] = (unsigned char)((value1 << 4) | (value2 >> 2));
            if (value3 != -2) {
                if (produced + 1 > capacity) {
                    return PDB_BUFFER_TOO_SMALL;
                }
                output[produced++] = (unsigned char)((value2 << 6) | value3);
            }
        }
    }
    *outLength = produced;
    return PDB_OK;
}

static int pdb_parse_int64(const char* text, __int64* outValue)
{
    const char* cursor;
    int negative;
    unsigned __int64 value;
    unsigned __int64 limit;
    int digit;

    if (text == NULL || outValue == NULL || text[0] == '\0') {
        return 0;
    }
    cursor = text;
    negative = 0;
    if (*cursor == '-') {
        negative = 1;
        ++cursor;
    } else if (*cursor == '+') {
        ++cursor;
    }
    if (*cursor == '\0') {
        return 0;
    }
    /* Accumulate against the absolute limit.  The negative range has one
     * additional representable value: -9223372036854775808. */
    limit = ((unsigned __int64)0x7FFFFFFFFFFFFFFF) +
            (negative ? 1 : 0);
    value = 0;
    while (*cursor != '\0') {
        if (*cursor < '0' || *cursor > '9') {
            return 0;
        }
        digit = *cursor - '0';
        if (value > (limit - (unsigned __int64)digit) / 10) {
            return 0;
        }
        value = value * 10 + (unsigned __int64)digit;
        ++cursor;
    }
    if (negative && value == ((unsigned __int64)0x7FFFFFFFFFFFFFFF + 1)) {
        *outValue = -(__int64)0x7FFFFFFFFFFFFFFF - 1;
    } else {
        *outValue = negative ? -(__int64)value : (__int64)value;
    }
    return 1;
}

static int pdb_parse_int(const char* text, int* outValue)
{
    __int64 value;

    if (!pdb_parse_int64(text, &value) ||
            value < (-2147483647 - 1) || value > 2147483647) {
        return 0;
    }
    *outValue = (int)value;
    return 1;
}

static int pdb_parse_version(const char* text, __int64* outValue)
{
    if (text == NULL || outValue == NULL || text[0] == '+' ||
            text[0] == '-') {
        return 0;
    }
    return pdb_parse_int64(text, outValue) && *outValue >= 0;
}

static int pdb_parse_double(const char* text, double* outValue)
{
    char* end;
    double value;

    if (text == NULL || outValue == NULL || text[0] == '\0' ||
            isspace((unsigned char)text[0])) {
        return 0;
    }
    end = NULL;
    value = strtod(text, &end);
    if (end == text || end == NULL || *end != '\0' ||
            value != value ||
            value > DBL_MAX || value < -DBL_MAX) {
        return 0;
    }
    *outValue = value;
    return 1;
}

static int pdb_prepare_internal(PDbHandle db, const char* sql,
        sqlite3_stmt** outStmt)
{
    int rc;
    const char* tail;
    int was_internal;

    if (db == NULL || db->sqlite == NULL || sql == NULL || outStmt == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    was_internal = db->internal;
    db->internal = 1;
    rc = sqlite3_prepare_v2(db->sqlite, sql, -1, outStmt, &tail);
    db->internal = was_internal;
    if (rc != SQLITE_OK) {
        return pdb_set_sqlite_error(db, rc);
    }
    return PDB_OK;
}

static int pdb_step_internal(PDbHandle db, sqlite3_stmt* stmt)
{
    int rc;
    int was_internal;

    was_internal = db->internal;
    db->internal = 1;
    rc = sqlite3_step(stmt);
    db->internal = was_internal;
    return rc;
}

static int pdb_exec_internal(PDbHandle db, const char* sql)
{
    int rc;
    char* error_message;
    int was_internal;

    if (db == NULL || sql == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    error_message = NULL;
    was_internal = db->internal;
    db->internal = 1;
    rc = sqlite3_exec(db->sqlite, sql, NULL, NULL, &error_message);
    db->internal = was_internal;
    if (rc != SQLITE_OK) {
        if (error_message != NULL) {
            pdb_set_error(db, error_message);
            sqlite3_free(error_message);
        }
        return pdb_set_sqlite_error(db, rc);
    }
    return PDB_OK;
}

static int pdb_meta_get(PDbHandle db, const char* key,
        char* value, int capacity)
{
    sqlite3_stmt* stmt;
    int rc;
    const unsigned char* text;

    if (db == NULL || key == NULL || value == NULL || capacity <= 0) {
        return PDB_INVALID_ARGUMENT;
    }
    value[0] = '\0';
    rc = pdb_prepare_internal(db,
            "SELECT value FROM __pdb_meta WHERE key=?1", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    rc = pdb_step_internal(db, stmt);
    if (rc == SQLITE_ROW) {
        text = sqlite3_column_text(stmt, 0);
        pdb_copy_text(value, capacity, (const char*)text);
        rc = PDB_OK;
    } else if (rc == SQLITE_DONE) {
        rc = PDB_NOT_FOUND;
    } else {
        rc = pdb_set_sqlite_error(db, rc);
    }
    sqlite3_finalize(stmt);
    return rc;
}

static int pdb_meta_set(PDbHandle db, const char* key, const char* value)
{
    sqlite3_stmt* stmt;
    int rc;

    if (db == NULL || key == NULL || value == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    rc = pdb_prepare_internal(db,
            "INSERT OR REPLACE INTO __pdb_meta(key,value) VALUES(?1,?2)",
            &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    rc = pdb_step_internal(db, stmt);
    if (rc == SQLITE_DONE) {
        rc = PDB_OK;
    } else {
        rc = pdb_set_sqlite_error(db, rc);
    }
    sqlite3_finalize(stmt);
    return rc;
}

static int pdb_meta_set_int(PDbHandle db, const char* key, int value)
{
    char text[32];

    _snprintf(text, sizeof(text) - 1, "%d", value);
    text[sizeof(text) - 1] = '\0';
    return pdb_meta_set(db, key, text);
}

static int pdb_meta_get_int(PDbHandle db, const char* key, int default_value)
{
    char text[32];
    int value;

    if (pdb_meta_get(db, key, text, sizeof(text)) != PDB_OK ||
            !pdb_parse_int(text, &value)) {
        return default_value;
    }
    return value;
}

static int pdb_authorizer(void* context, int action,
        const char* arg1, const char* arg2, const char* database,
        const char* trigger)
{
    PDbHandle db;
    int reserved_action;

    (void)database;
    db = (PDbHandle)context;
    if (db == NULL || db->mode != PDB_OPEN_SYNC) {
        return SQLITE_OK;
    }
    reserved_action = action == SQLITE_READ || action == SQLITE_INSERT ||
            action == SQLITE_UPDATE || action == SQLITE_DELETE ||
            action == SQLITE_CREATE_INDEX || action == SQLITE_CREATE_TABLE ||
            action == SQLITE_CREATE_TEMP_INDEX ||
            action == SQLITE_CREATE_TEMP_TABLE ||
            action == SQLITE_CREATE_TEMP_TRIGGER ||
            action == SQLITE_CREATE_TRIGGER || action == SQLITE_CREATE_VIEW ||
            action == SQLITE_CREATE_TEMP_VIEW || action == SQLITE_DROP_INDEX ||
            action == SQLITE_DROP_TABLE || action == SQLITE_DROP_TEMP_INDEX ||
            action == SQLITE_DROP_TEMP_TABLE ||
            action == SQLITE_DROP_TEMP_TRIGGER || action == SQLITE_DROP_TRIGGER ||
            action == SQLITE_DROP_VIEW || action == SQLITE_DROP_TEMP_VIEW ||
            action == SQLITE_ALTER_TABLE || action == SQLITE_CREATE_VTABLE ||
            action == SQLITE_DROP_VTABLE;
    if (reserved_action && arg1 != NULL &&
            strncmp(arg1, "__pdb_", 6) == 0 &&
            (db->migration || (!db->internal && trigger == NULL))) {
        return SQLITE_DENY;
    }
    /* Migration scripts may contain schema DDL, but never connection
     * attachment, PRAGMA changes, or extension loading.  Internal setup
     * statements remain trusted after these dangerous classes are checked. */
    if (db->migration &&
            (action == SQLITE_ATTACH || action == SQLITE_DETACH ||
             action == SQLITE_PRAGMA || action == SQLITE_TRANSACTION ||
             action == SQLITE_SAVEPOINT)) {
        return SQLITE_DENY;
    }
    if (db->migration && action == SQLITE_FUNCTION &&
            ((arg1 != NULL && _stricmp(arg1, "load_extension") == 0) ||
             (arg2 != NULL && _stricmp(arg2, "load_extension") == 0))) {
        return SQLITE_DENY;
    }
    if (db->internal) {
        return SQLITE_OK;
    }
    switch (action) {
    case SQLITE_ATTACH:
    case SQLITE_DETACH:
    case SQLITE_PRAGMA:
    case SQLITE_CREATE_INDEX:
    case SQLITE_CREATE_TABLE:
    case SQLITE_CREATE_TEMP_INDEX:
    case SQLITE_CREATE_TEMP_TABLE:
    case SQLITE_CREATE_TEMP_TRIGGER:
    case SQLITE_CREATE_TRIGGER:
    case SQLITE_CREATE_VIEW:
    case SQLITE_CREATE_TEMP_VIEW:
    case SQLITE_DROP_INDEX:
    case SQLITE_DROP_TABLE:
    case SQLITE_DROP_TEMP_INDEX:
    case SQLITE_DROP_TEMP_TABLE:
    case SQLITE_DROP_TEMP_TRIGGER:
    case SQLITE_DROP_TRIGGER:
    case SQLITE_DROP_VIEW:
    case SQLITE_DROP_TEMP_VIEW:
    case SQLITE_ALTER_TABLE:
    case SQLITE_CREATE_VTABLE:
    case SQLITE_DROP_VTABLE:
    case SQLITE_TRANSACTION:
    case SQLITE_SAVEPOINT:
        return SQLITE_DENY;
    case SQLITE_FUNCTION:
        if ((arg1 != NULL && _stricmp(arg1, "load_extension") == 0) ||
                (arg2 != NULL && _stricmp(arg2, "load_extension") == 0)) {
            return SQLITE_DENY;
        }
        break;
    default:
        break;
    }
    return SQLITE_OK;
}

static int pdb_progress(void* context)
{
    PDbHandle db;

    db = (PDbHandle)context;
    return db != NULL && db->cancelled ? 1 : 0;
}

static int pdb_find_table(PDbHandle db, const char* table_name)
{
    int index;

    if (db == NULL || table_name == NULL) {
        return -1;
    }
    for (index = 0; index < db->table_count; ++index) {
        if (db->tables[index].used &&
                strcmp(db->tables[index].table_name, table_name) == 0) {
            return index;
        }
    }
    return -1;
}

static int pdb_make_columns_csv(const PDbSyncColumn* columns,
        int column_count, char* output, int capacity)
{
    int index;
    int used;
    int length;

    if (columns == NULL || output == NULL || capacity <= 0 ||
            column_count <= 0 || column_count > PDB_SYNC_MAX_COLUMNS) {
        return PDB_INVALID_ARGUMENT;
    }
    output[0] = '\0';
    used = 0;
    for (index = 0; index < column_count; ++index) {
        if (!pdb_is_identifier(columns[index].name) ||
                pdb_is_reserved_name(columns[index].name)) {
            return PDB_INVALID_ARGUMENT;
        }
        length = (int)strlen(columns[index].name);
        if (used + length + (index == 0 ? 0 : 1) + 1 > capacity) {
            return PDB_LIMIT;
        }
        if (index != 0) {
            output[used++] = ',';
        }
        memcpy(output + used, columns[index].name, (size_t)length);
        used += length;
        output[used] = '\0';
    }
    return PDB_OK;
}

static int pdb_parse_columns_csv(PDbTableInfo* table, const char* csv)
{
    const char* cursor;
    const char* comma;
    int length;
    int count;

    if (table == NULL || csv == NULL || csv[0] == '\0') {
        return PDB_INVALID_ARGUMENT;
    }
    cursor = csv;
    count = 0;
    while (*cursor != '\0') {
        comma = strchr(cursor, ',');
        length = comma == NULL ? (int)strlen(cursor) :
                (int)(comma - cursor);
        if (length <= 0 || length >= PDB_SYNC_NAME_MAX ||
                count >= PDB_SYNC_MAX_COLUMNS) {
            return PDB_LIMIT;
        }
        memcpy(table->columns[count], cursor, (size_t)length);
        table->columns[count][length] = '\0';
        if (!pdb_is_identifier(table->columns[count])) {
            return PDB_INVALID_ARGUMENT;
        }
        table->column_types[count] = PDB_VALUE_NULL;
        count += 1;
        if (comma == NULL) {
            break;
        }
        cursor = comma + 1;
    }
    table->column_count = count;
    return count > 0 ? PDB_OK : PDB_INVALID_ARGUMENT;
}

static int pdb_create_sync_schema(PDbHandle db)
{
    static const char schema[] =
        "CREATE TABLE IF NOT EXISTS __pdb_sync_tables("
        "table_name TEXT PRIMARY KEY,"
        "primary_key TEXT NOT NULL,"
        "columns TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS __pdb_row_state("
        "table_name TEXT NOT NULL,"
        "row_key TEXT NOT NULL,"
        "server_version TEXT NOT NULL,"
        "deleted INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(table_name,row_key));"
        "CREATE TABLE IF NOT EXISTS __pdb_dirty("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "table_name TEXT NOT NULL,"
        "row_key TEXT NOT NULL,"
        "action INTEGER NOT NULL,"
        "UNIQUE(table_name,row_key));"
        "CREATE TABLE IF NOT EXISTS __pdb_outbox("
        "sequence INTEGER PRIMARY KEY AUTOINCREMENT,"
        "table_name TEXT NOT NULL,"
        "row_key TEXT NOT NULL,"
        "action INTEGER NOT NULL,"
        "base_version TEXT NOT NULL,"
        "row_json TEXT,"
        "UNIQUE(table_name,row_key));"
        "CREATE TABLE IF NOT EXISTS __pdb_conflict("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "table_name TEXT NOT NULL,"
        "row_key TEXT NOT NULL,"
        "op_sequence INTEGER NOT NULL DEFAULT 0,"
        "server_version TEXT NOT NULL,"
        "local_row_json TEXT,"
        "server_row_json TEXT NOT NULL);";

    return pdb_exec_internal(db, schema);
}

static int pdb_validate_table_exists(PDbHandle db, const char* table_name)
{
    sqlite3_stmt* stmt;
    int rc;

    rc = pdb_prepare_internal(db,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1",
            &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
    rc = pdb_step_internal(db, stmt);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_ROW) {
        return PDB_OK;
    }
    if (rc == SQLITE_DONE) {
        pdb_set_error(db, "sync table does not exist");
        return PDB_NOT_FOUND;
    }
    return pdb_set_sqlite_error(db, rc);
}

static int pdb_install_triggers(PDbHandle db, const PDbTableInfo* table)
{
    char quoted_table[128];
    char quoted_key[128];
    char trigger_name[128];
    char quoted_trigger[144];
    char sql[1536];
    int rc;

    if (db == NULL || table == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    if (!pdb_quote_identifier(quoted_table, sizeof(quoted_table),
            table->table_name) ||
            !pdb_quote_identifier(quoted_key, sizeof(quoted_key),
            table->primary_key)) {
        return PDB_INVALID_ARGUMENT;
    }

    _snprintf(trigger_name, sizeof(trigger_name) - 1,
            "__pdb_%s_ai", table->table_name);
    trigger_name[sizeof(trigger_name) - 1] = '\0';
    if (!pdb_quote_identifier(quoted_trigger, sizeof(quoted_trigger),
            trigger_name)) {
        return PDB_LIMIT;
    }
    _snprintf(sql, sizeof(sql) - 1, "DROP TRIGGER IF EXISTS %s",
            quoted_trigger);
    sql[sizeof(sql) - 1] = '\0';
    rc = pdb_exec_internal(db, sql);
    if (rc != PDB_OK) {
        return rc;
    }
    _snprintf(sql, sizeof(sql) - 1,
            "CREATE TRIGGER IF NOT EXISTS %s AFTER INSERT ON %s "
            "BEGIN INSERT OR REPLACE INTO __pdb_dirty"
            "(table_name,row_key,action) VALUES('%s',"
            "CAST(NEW.%s AS TEXT),1); END",
            quoted_trigger, quoted_table, table->table_name, quoted_key);
    sql[sizeof(sql) - 1] = '\0';
    rc = pdb_exec_internal(db, sql);
    if (rc != PDB_OK) {
        return rc;
    }

    _snprintf(trigger_name, sizeof(trigger_name) - 1,
            "__pdb_%s_au", table->table_name);
    trigger_name[sizeof(trigger_name) - 1] = '\0';
    if (!pdb_quote_identifier(quoted_trigger, sizeof(quoted_trigger),
            trigger_name)) {
        return PDB_LIMIT;
    }
    _snprintf(sql, sizeof(sql) - 1, "DROP TRIGGER IF EXISTS %s",
            quoted_trigger);
    sql[sizeof(sql) - 1] = '\0';
    rc = pdb_exec_internal(db, sql);
    if (rc != PDB_OK) {
        return rc;
    }
    _snprintf(sql, sizeof(sql) - 1,
            "CREATE TRIGGER IF NOT EXISTS %s AFTER UPDATE ON %s "
            "BEGIN INSERT OR REPLACE INTO __pdb_dirty"
            "(table_name,row_key,action) VALUES('%s',"
            "CAST(OLD.%s AS TEXT),2); INSERT OR REPLACE INTO __pdb_dirty"
            "(table_name,row_key,action) VALUES('%s',"
            "CAST(NEW.%s AS TEXT),1); END",
            quoted_trigger, quoted_table, table->table_name, quoted_key,
            table->table_name, quoted_key);
    sql[sizeof(sql) - 1] = '\0';
    rc = pdb_exec_internal(db, sql);
    if (rc != PDB_OK) {
        return rc;
    }

    _snprintf(trigger_name, sizeof(trigger_name) - 1,
            "__pdb_%s_ad", table->table_name);
    trigger_name[sizeof(trigger_name) - 1] = '\0';
    if (!pdb_quote_identifier(quoted_trigger, sizeof(quoted_trigger),
            trigger_name)) {
        return PDB_LIMIT;
    }
    _snprintf(sql, sizeof(sql) - 1, "DROP TRIGGER IF EXISTS %s",
            quoted_trigger);
    sql[sizeof(sql) - 1] = '\0';
    rc = pdb_exec_internal(db, sql);
    if (rc != PDB_OK) {
        return rc;
    }
    _snprintf(sql, sizeof(sql) - 1,
            "CREATE TRIGGER IF NOT EXISTS %s AFTER DELETE ON %s "
            "BEGIN INSERT OR REPLACE INTO __pdb_dirty"
            "(table_name,row_key,action) VALUES('%s',"
            "CAST(OLD.%s AS TEXT),2); END",
            quoted_trigger, quoted_table, table->table_name, quoted_key);
    sql[sizeof(sql) - 1] = '\0';
    return pdb_exec_internal(db, sql);
}

static int pdb_contains_ascii_ci(const char* value, const char* token)
{
    int value_length;
    int token_length;
    int index;

    if (value == NULL || token == NULL) {
        return 0;
    }
    value_length = (int)strlen(value);
    token_length = (int)strlen(token);
    if (token_length == 0 || value_length < token_length) {
        return 0;
    }
    for (index = 0; index <= value_length - token_length; ++index) {
        if (_strnicmp(value + index, token, token_length) == 0) {
            return 1;
        }
    }
    return 0;
}

static int pdb_validate_table_schema(PDbHandle db,
        const PDbTableInfo* table)
{
    sqlite3_stmt* stmt;
    char quoted_table[128];
    char sql[192];
    int seen[PDB_SYNC_MAX_COLUMNS];
    int actual_count;
    int key_count;
    int total_key_count;
    int key_notnull;
    int key_type;
    int index;
    int column_index;
    int pk;
    const unsigned char* name;
    const unsigned char* declared_type;
    int rc;

    if (db == NULL || table == NULL ||
            !pdb_quote_identifier(quoted_table, sizeof(quoted_table),
            table->table_name)) {
        return PDB_INVALID_ARGUMENT;
    }
    memset(seen, 0, sizeof(seen));
    actual_count = 0;
    key_count = 0;
    total_key_count = 0;
    key_notnull = 0;
    key_type = PDB_VALUE_NULL;
    _snprintf(sql, sizeof(sql) - 1, "PRAGMA table_info(%s)",
            quoted_table);
    sql[sizeof(sql) - 1] = '\0';
    rc = pdb_prepare_internal(db, sql, &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    while ((rc = pdb_step_internal(db, stmt)) == SQLITE_ROW) {
        name = sqlite3_column_text(stmt, 1);
        declared_type = sqlite3_column_text(stmt, 2);
        pk = sqlite3_column_int(stmt, 5);
        if (name == NULL || actual_count >= PDB_SYNC_MAX_COLUMNS) {
            sqlite3_finalize(stmt);
            pdb_set_error(db, "sync table has too many or invalid columns");
            return PDB_SCHEMA_MISMATCH;
        }
        actual_count += 1;
        if (pk > 0) {
            total_key_count += 1;
        }
        column_index = -1;
        for (index = 0; index < table->column_count; ++index) {
            if (strcmp((const char*)name, table->columns[index]) == 0) {
                if (seen[index]) {
                    sqlite3_finalize(stmt);
                    pdb_set_error(db, "sync table column is registered twice");
                    return PDB_SCHEMA_MISMATCH;
                }
                seen[index] = 1;
                column_index = index;
                break;
            }
        }
        if (strcmp((const char*)name, table->primary_key) == 0) {
            key_count += pk > 0 ? 1 : 0;
            key_notnull = sqlite3_column_int(stmt, 3);
            if (declared_type != NULL &&
                    pdb_contains_ascii_ci((const char*)declared_type,
                    "INT")) {
                key_type = PDB_VALUE_INTEGER;
            } else if (declared_type != NULL &&
                    (pdb_contains_ascii_ci((const char*)declared_type,
                    "CHAR") ||
                     pdb_contains_ascii_ci((const char*)declared_type,
                    "CLOB") ||
                     pdb_contains_ascii_ci((const char*)declared_type,
                    "TEXT"))) {
                key_type = PDB_VALUE_TEXT;
            }
        }
        (void)column_index;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return pdb_set_sqlite_error(db, rc);
    }
    if (actual_count != table->column_count || total_key_count != 1 ||
            key_count != 1 ||
            key_type == PDB_VALUE_NULL ||
            (!key_notnull && key_type != PDB_VALUE_INTEGER)) {
        pdb_set_error(db, "sync table schema does not match its registration");
        return PDB_SCHEMA_MISMATCH;
    }
    for (index = 0; index < table->column_count; ++index) {
        if (!seen[index]) {
            pdb_set_error(db, "registered sync column is not in the table");
            return PDB_SCHEMA_MISMATCH;
        }
        if (strcmp(table->columns[index], table->primary_key) == 0 &&
                table->column_types[index] != PDB_VALUE_NULL &&
                table->column_types[index] != key_type) {
            pdb_set_error(db, "sync primary key type does not match");
            return PDB_SCHEMA_MISMATCH;
        }
        if (table->columns[index][0] == '\0') {
            return PDB_INVALID_ARGUMENT;
        }
    }
    return PDB_OK;
}

static int pdb_load_tables(PDbHandle db)
{
    sqlite3_stmt* stmt;
    int rc;
    const unsigned char* table_name;
    const unsigned char* primary_key;
    const unsigned char* columns;
    PDbTableInfo* table;
    int table_index;

    rc = pdb_prepare_internal(db,
            "SELECT table_name,primary_key,columns FROM __pdb_sync_tables "
            "ORDER BY table_name", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    while ((rc = pdb_step_internal(db, stmt)) == SQLITE_ROW) {
        if (db->table_count >= PDB_SYNC_MAX_TABLES) {
            sqlite3_finalize(stmt);
            return PDB_LIMIT;
        }
        table = &db->tables[db->table_count];
        memset(table, 0, sizeof(*table));
        table_name = sqlite3_column_text(stmt, 0);
        primary_key = sqlite3_column_text(stmt, 1);
        columns = sqlite3_column_text(stmt, 2);
        if (table_name == NULL || primary_key == NULL || columns == NULL ||
                !pdb_is_identifier((const char*)table_name) ||
                !pdb_is_identifier((const char*)primary_key)) {
            sqlite3_finalize(stmt);
            return PDB_ERROR;
        }
        pdb_copy_text(table->table_name, sizeof(table->table_name),
                (const char*)table_name);
        pdb_copy_text(table->primary_key, sizeof(table->primary_key),
                (const char*)primary_key);
        rc = pdb_parse_columns_csv(table, (const char*)columns);
        if (rc != PDB_OK) {
            sqlite3_finalize(stmt);
            return rc;
        }
        table->used = 1;
        db->table_count += 1;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return pdb_set_sqlite_error(db, rc);
    }
    for (table_index = 0; table_index < db->table_count; ++table_index) {
        rc = pdb_validate_table_exists(db,
                db->tables[table_index].table_name);
        if (rc == PDB_OK) {
            rc = pdb_validate_table_schema(db, &db->tables[table_index]);
        }
        if (rc == PDB_OK) {
            rc = pdb_install_triggers(db, &db->tables[table_index]);
        }
        if (rc != PDB_OK) {
            return rc;
        }
    }
    return pdb_drain_dirty(db);
}

static int pdb_store_table(PDbHandle db, const PDbTableInfo* table)
{
    sqlite3_stmt* stmt;
    char columns[PDB_SYNC_MAX_COLUMNS * PDB_SYNC_NAME_MAX];
    int index;
    int used;
    int length;
    int rc;

    columns[0] = '\0';
    used = 0;
    for (index = 0; index < table->column_count; ++index) {
        length = (int)strlen(table->columns[index]);
        if (used + length + (index == 0 ? 0 : 1) + 1 >
                (int)sizeof(columns)) {
            return PDB_LIMIT;
        }
        if (index != 0) {
            columns[used++] = ',';
        }
        memcpy(columns + used, table->columns[index], (size_t)length);
        used += length;
        columns[used] = '\0';
    }
    rc = pdb_prepare_internal(db,
            "INSERT OR REPLACE INTO __pdb_sync_tables"
            "(table_name,primary_key,columns) VALUES(?1,?2,?3)", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, table->table_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, table->primary_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, columns, -1, SQLITE_TRANSIENT);
    rc = pdb_step_internal(db, stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return pdb_set_sqlite_error(db, rc);
    }
    return PDB_OK;
}

static int pdb_row_writer_value(PDbWriter* writer, sqlite3_stmt* stmt,
        int column)
{
    int type;
    const unsigned char* text;
    const void* blob;
    int bytes;

    pdb_writer_char(writer, '{');
    pdb_writer_literal(writer, "\"t\":");
    type = sqlite3_column_type(stmt, column);
    if (type == SQLITE_NULL) {
        pdb_writer_json_string(writer, "n");
        pdb_writer_literal(writer, ",\"v\":null}");
    } else if (type == SQLITE_INTEGER) {
        pdb_writer_json_string(writer, "i");
        pdb_writer_literal(writer, ",\"v\":");
        pdb_writer_char(writer, '"');
        pdb_writer_int64(writer, sqlite3_column_int64(stmt, column));
        pdb_writer_char(writer, '"');
        pdb_writer_char(writer, '}');
    } else if (type == SQLITE_FLOAT) {
        pdb_writer_json_string(writer, "r");
        pdb_writer_literal(writer, ",\"v\":");
        pdb_writer_char(writer, '"');
        pdb_writer_double(writer, sqlite3_column_double(stmt, column));
        pdb_writer_char(writer, '"');
        pdb_writer_char(writer, '}');
    } else if (type == SQLITE_TEXT) {
        text = sqlite3_column_text(stmt, column);
        pdb_writer_json_string(writer, "s");
        pdb_writer_literal(writer, ",\"v\":");
        pdb_writer_json_string(writer, (const char*)text);
        pdb_writer_char(writer, '}');
    } else if (type == SQLITE_BLOB) {
        blob = sqlite3_column_blob(stmt, column);
        bytes = sqlite3_column_bytes(stmt, column);
        if (pdb_base64_encoded_size(bytes) < 0) {
            writer->overflow = 1;
        }
        pdb_writer_json_string(writer, "b");
        pdb_writer_literal(writer, ",\"v\":\"");
        pdb_base64_encode(writer, (const unsigned char*)blob, bytes);
        pdb_writer_literal(writer, "\"}");
    } else {
        writer->overflow = 1;
    }
    return writer->overflow ? PDB_LIMIT : PDB_OK;
}

static int pdb_row_to_json(PDbHandle db, const PDbTableInfo* table,
        const char* row_key, char** outJson)
{
    sqlite3_stmt* stmt;
    char sql[8192];
    char quoted_table[128];
    char quoted_key[128];
    char quoted_column[128];
    char* json;
    PDbWriter writer;
    int index;
    int used;
    int rc;

    if (db == NULL || table == NULL || row_key == NULL || outJson == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    *outJson = NULL;
    if (!pdb_quote_identifier(quoted_table, sizeof(quoted_table),
            table->table_name) ||
            !pdb_quote_identifier(quoted_key, sizeof(quoted_key),
            table->primary_key)) {
        return PDB_INVALID_ARGUMENT;
    }
    sql[0] = '\0';
    used = _snprintf(sql, sizeof(sql) - 1, "SELECT ");
    if (used < 0) {
        return PDB_LIMIT;
    }
    for (index = 0; index < table->column_count; ++index) {
        if (!pdb_quote_identifier(quoted_column, sizeof(quoted_column),
                table->columns[index])) {
            return PDB_INVALID_ARGUMENT;
        }
        used += _snprintf(sql + used, sizeof(sql) - used - 1, "%s%s",
                index == 0 ? "" : ",", quoted_column);
        if (used < 0 || used >= (int)sizeof(sql) - 1) {
            return PDB_LIMIT;
        }
    }
    used += _snprintf(sql + used, sizeof(sql) - used - 1,
            " FROM %s WHERE CAST(%s AS TEXT)=?1 LIMIT 1",
            quoted_table, quoted_key);
    if (used < 0 || used >= (int)sizeof(sql) - 1) {
        return PDB_LIMIT;
    }
    rc = pdb_prepare_internal(db, sql, &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, row_key, -1, SQLITE_TRANSIENT);
    rc = pdb_step_internal(db, stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return PDB_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        rc = pdb_set_sqlite_error(db, rc);
        sqlite3_finalize(stmt);
        return rc;
    }
    json = (char*)malloc(PDB_MAX_ROW_BYTES);
    if (json == NULL) {
        sqlite3_finalize(stmt);
        return PDB_NOMEM;
    }
    pdb_writer_init(&writer, json, PDB_MAX_ROW_BYTES);
    pdb_writer_char(&writer, '{');
    for (index = 0; index < table->column_count; ++index) {
        if (index != 0) {
            pdb_writer_char(&writer, ',');
        }
        pdb_writer_json_string(&writer, table->columns[index]);
        pdb_writer_char(&writer, ':');
        if (pdb_row_writer_value(&writer, stmt, index) != PDB_OK) {
            free(json);
            sqlite3_finalize(stmt);
            return PDB_LIMIT;
        }
    }
    pdb_writer_char(&writer, '}');
    sqlite3_finalize(stmt);
    if (writer.overflow) {
        free(json);
        return PDB_LIMIT;
    }
    *outJson = json;
    return PDB_OK;
}

static int pdb_get_row_state(PDbHandle db, const char* table_name,
        const char* row_key, char* version, int capacity)
{
    sqlite3_stmt* stmt;
    const unsigned char* text;
    int rc;

    if (version == NULL || capacity <= 0) {
        return PDB_INVALID_ARGUMENT;
    }
    version[0] = '\0';
    rc = pdb_prepare_internal(db,
            "SELECT server_version FROM __pdb_row_state "
            "WHERE table_name=?1 AND row_key=?2", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, row_key, -1, SQLITE_TRANSIENT);
    rc = pdb_step_internal(db, stmt);
    if (rc == SQLITE_ROW) {
        text = sqlite3_column_text(stmt, 0);
        pdb_copy_text(version, capacity, (const char*)text);
        rc = PDB_OK;
    } else if (rc == SQLITE_DONE) {
        pdb_copy_text(version, capacity, "0");
        rc = PDB_OK;
    } else {
        rc = pdb_set_sqlite_error(db, rc);
    }
    sqlite3_finalize(stmt);
    return rc;
}

static int pdb_set_row_state(PDbHandle db, const char* table_name,
        const char* row_key, const char* version, int deleted)
{
    sqlite3_stmt* stmt;
    int rc;

    rc = pdb_prepare_internal(db,
            "INSERT OR REPLACE INTO __pdb_row_state"
            "(table_name,row_key,server_version,deleted)"
            "VALUES(?1,?2,?3,?4)", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, row_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, version == NULL ? "0" : version,
            -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, deleted ? 1 : 0);
    rc = pdb_step_internal(db, stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return pdb_set_sqlite_error(db, rc);
    }
    return PDB_OK;
}

static int pdb_find_pending(PDbHandle db, const char* table_name,
        const char* row_key, __int64* sequence, char* base_version,
        int base_capacity)
{
    sqlite3_stmt* stmt;
    const unsigned char* version;
    int rc;

    if (sequence == NULL || base_version == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    *sequence = 0;
    base_version[0] = '\0';
    rc = pdb_prepare_internal(db,
            "SELECT sequence,base_version FROM __pdb_outbox "
            "WHERE table_name=?1 AND row_key=?2", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, row_key, -1, SQLITE_TRANSIENT);
    rc = pdb_step_internal(db, stmt);
    if (rc == SQLITE_ROW) {
        *sequence = sqlite3_column_int64(stmt, 0);
        version = sqlite3_column_text(stmt, 1);
        pdb_copy_text(base_version, base_capacity, (const char*)version);
        rc = PDB_OK;
    } else if (rc == SQLITE_DONE) {
        rc = PDB_NOT_FOUND;
    } else {
        rc = pdb_set_sqlite_error(db, rc);
    }
    sqlite3_finalize(stmt);
    return rc;
}

static int pdb_append_outbox(PDbHandle db, const char* table_name,
        const char* row_key, int action, const char* row_json,
        const char* explicit_base_version)
{
    sqlite3_stmt* stmt;
    char base_version[PDB_SYNC_NAME_MAX];
    __int64 sequence;
    int rc;

    if (db == NULL || table_name == NULL || row_key == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    base_version[0] = '\0';
    rc = pdb_find_pending(db, table_name, row_key, &sequence,
            base_version, sizeof(base_version));
    if (rc != PDB_OK && rc != PDB_NOT_FOUND) {
        return rc;
    }
    if (explicit_base_version != NULL) {
        pdb_copy_text(base_version, sizeof(base_version),
                explicit_base_version);
    } else if (rc == PDB_NOT_FOUND) {
        rc = pdb_get_row_state(db, table_name, row_key,
                base_version, sizeof(base_version));
        if (rc != PDB_OK) {
            return rc;
        }
    }

    if (sequence != 0) {
        rc = pdb_prepare_internal(db,
                "UPDATE __pdb_outbox SET action=?1,row_json=?2 "
                "WHERE sequence=?3", &stmt);
        if (rc != PDB_OK) {
            return rc;
        }
        sqlite3_bind_int(stmt, 1, action);
        if (row_json == NULL) {
            sqlite3_bind_null(stmt, 2);
        } else {
            sqlite3_bind_text(stmt, 2, row_json, -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_int64(stmt, 3, sequence);
    } else {
        rc = pdb_prepare_internal(db,
                "INSERT INTO __pdb_outbox"
                "(table_name,row_key,action,base_version,row_json)"
                "VALUES(?1,?2,?3,?4,?5)", &stmt);
        if (rc != PDB_OK) {
            return rc;
        }
        sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, row_key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, action);
        sqlite3_bind_text(stmt, 4, base_version, -1, SQLITE_TRANSIENT);
        if (row_json == NULL) {
            sqlite3_bind_null(stmt, 5);
        } else {
            sqlite3_bind_text(stmt, 5, row_json, -1, SQLITE_TRANSIENT);
        }
    }
    rc = pdb_step_internal(db, stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return pdb_set_sqlite_error(db, rc);
    }
    return PDB_OK;
}

static int pdb_delete_dirty(PDbHandle db, const char* table_name,
        const char* row_key)
{
    sqlite3_stmt* stmt;
    int rc;

    rc = pdb_prepare_internal(db,
            "DELETE FROM __pdb_dirty WHERE table_name=?1 AND row_key=?2",
            &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, row_key, -1, SQLITE_TRANSIENT);
    rc = pdb_step_internal(db, stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return pdb_set_sqlite_error(db, rc);
    }
    return PDB_OK;
}

static int pdb_drain_dirty(PDbHandle db)
{
    sqlite3_stmt* stmt;
    char table_name[PDB_SYNC_NAME_MAX];
    char row_key[PDB_SYNC_NAME_MAX];
    int table_index;
    int action;
    int rc;
    int started;
    char* row_json;

    if (db == NULL || db->mode != PDB_OPEN_SYNC || db->applying) {
        return PDB_OK;
    }
    started = 0;
    rc = PDB_OK;
    for (;;) {
        rc = pdb_prepare_internal(db,
                "SELECT table_name,row_key,action FROM __pdb_dirty "
                "ORDER BY id LIMIT 1", &stmt);
        if (rc != PDB_OK) {
            break;
        }
        rc = pdb_step_internal(db, stmt);
        if (rc == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            rc = PDB_OK;
            break;
        }
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            rc = pdb_set_sqlite_error(db, rc);
            break;
        }
        if (sqlite3_column_text(stmt, 0) == NULL ||
                sqlite3_column_text(stmt, 1) == NULL) {
            sqlite3_finalize(stmt);
            pdb_set_error(db, "sync dirty row is invalid");
            rc = PDB_ERROR;
            break;
        }
        pdb_copy_text(table_name, sizeof(table_name),
                (const char*)sqlite3_column_text(stmt, 0));
        pdb_copy_text(row_key, sizeof(row_key),
                (const char*)sqlite3_column_text(stmt, 1));
        action = sqlite3_column_int(stmt, 2);
        sqlite3_finalize(stmt);

        if (!db->in_transaction && !started) {
            rc = pdb_exec_internal(db, "BEGIN IMMEDIATE");
            if (rc != PDB_OK) {
                break;
            }
            db->in_transaction = 1;
            started = 1;
        }
        table_index = pdb_find_table(db, table_name);
        if (table_index < 0) {
            pdb_set_error(db, "sync trigger references an unknown table");
            rc = PDB_STATE;
            break;
        }
        row_json = NULL;
        if (action == PDB_DIRTY_DELETE) {
            rc = pdb_append_outbox(db, table_name, row_key,
                    PDB_DIRTY_DELETE, NULL, NULL);
        } else if (action == PDB_DIRTY_UPSERT) {
            rc = pdb_row_to_json(db, &db->tables[table_index],
                    row_key, &row_json);
            if (rc == PDB_NOT_FOUND) {
                rc = pdb_append_outbox(db, table_name, row_key,
                        PDB_DIRTY_DELETE, NULL, NULL);
            } else if (rc == PDB_OK) {
                rc = pdb_append_outbox(db, table_name, row_key,
                        PDB_DIRTY_UPSERT, row_json, NULL);
            }
        } else {
            pdb_set_error(db, "sync dirty row has an invalid action");
            rc = PDB_ERROR;
        }
        if (row_json != NULL) {
            free(row_json);
        }
        if (rc != PDB_OK) {
            break;
        }
        rc = pdb_delete_dirty(db, table_name, row_key);
        if (rc != PDB_OK) {
            break;
        }
    }
    if (started) {
        if (rc == PDB_OK) {
            rc = pdb_exec_internal(db, "COMMIT");
            if (rc == PDB_OK) {
                db->in_transaction = 0;
            } else {
                pdb_exec_internal(db, "ROLLBACK");
                db->in_transaction = 0;
            }
        } else {
            pdb_exec_internal(db, "ROLLBACK");
            db->in_transaction = 0;
        }
    }
    return rc;
}

static int pdb_prepare_user(PDbHandle db, const char* sql,
        sqlite3_stmt** outStmt)
{
    const char* tail;
    const char* cursor;
    int rc;

    if (sql == NULL || (int)strlen(sql) > PDB_SQL_MAX_BYTES) {
        pdb_set_error(db, "SQL length exceeds the Positron DB limit");
        return PDB_LIMIT;
    }
    rc = sqlite3_prepare_v2(db->sqlite, sql, -1, outStmt, &tail);
    if (rc != SQLITE_OK) {
        return pdb_set_sqlite_error(db, rc);
    }
    cursor = tail;
    while (cursor != NULL && (*cursor == ' ' || *cursor == '\t' ||
            *cursor == '\r' || *cursor == '\n' || *cursor == ';')) {
        ++cursor;
    }
    if (cursor != NULL && *cursor != '\0') {
        sqlite3_finalize(*outStmt);
        *outStmt = NULL;
        pdb_set_error(db, "multiple SQL statements are not allowed");
        return PDB_SQL_REJECTED;
    }
    return PDB_OK;
}

static int pdb_open_metadata(PDbHandle db)
{
    char mode_text[32];
    char protocol_text[32];
    int stored_mode;
    int stored_protocol;
    int rc;

    rc = pdb_exec_internal(db,
            "CREATE TABLE IF NOT EXISTS __pdb_meta("
            "key TEXT PRIMARY KEY,value TEXT NOT NULL)");
    if (rc != PDB_OK) {
        return rc;
    }
    rc = pdb_meta_get(db, "mode", mode_text, sizeof(mode_text));
    if (rc == PDB_NOT_FOUND) {
        rc = pdb_meta_set_int(db, "mode", db->mode);
        if (rc != PDB_OK) {
            return rc;
        }
    } else if (rc != PDB_OK) {
        return rc;
    } else if (!pdb_parse_int(mode_text, &stored_mode) ||
            stored_mode != db->mode) {
        pdb_set_error(db, "database was opened with an incompatible mode");
        return PDB_STATE;
    }
    if (db->mode == PDB_OPEN_SYNC) {
        rc = pdb_meta_get(db, "protocol", protocol_text,
                sizeof(protocol_text));
        if (rc == PDB_NOT_FOUND) {
            rc = pdb_meta_set_int(db, "protocol", 1);
        } else if (rc == PDB_OK &&
                (!pdb_parse_int(protocol_text, &stored_protocol) ||
                 stored_protocol != 1)) {
            pdb_set_error(db, "database uses an unsupported sync protocol");
            rc = PDB_NOT_SUPPORTED;
        }
        if (rc != PDB_OK) {
            return rc;
        }
        rc = pdb_create_sync_schema(db);
        if (rc != PDB_OK) {
            return rc;
        }
        rc = pdb_load_tables(db);
        if (rc != PDB_OK) {
            return rc;
        }
    }
    return PDB_OK;
}

PDB_API int PDb_OpenUtf8(const char* path, int mode, PDbHandle* outDb)
{
    PDbHandle db;
    int flags;
    int rc;

    if (path == NULL || outDb == NULL ||
            (mode != PDB_OPEN_LOCAL_FULL_SQL && mode != PDB_OPEN_SYNC)) {
        return PDB_INVALID_ARGUMENT;
    }
    *outDb = NULL;
    db = (PDbHandle)malloc(sizeof(*db));
    if (db == NULL) {
        return PDB_NOMEM;
    }
    memset(db, 0, sizeof(*db));
    db->mode = mode;
    db->last_error[0] = '\0';
    flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    rc = sqlite3_open_v2(path, &db->sqlite, flags, NULL);
    if (rc != SQLITE_OK) {
        if (db->sqlite != NULL) {
            pdb_set_sqlite_error(db, rc);
            sqlite3_close(db->sqlite);
        }
        free(db);
        return rc == SQLITE_NOMEM ? PDB_NOMEM : PDB_ERROR;
    }
    sqlite3_busy_timeout(db->sqlite, 1000);
    sqlite3_limit(db->sqlite, SQLITE_LIMIT_LENGTH,
            PDB_SYNC_MAX_BODY_BYTES);
    sqlite3_limit(db->sqlite, SQLITE_LIMIT_SQL_LENGTH,
            PDB_SQL_MAX_BYTES);
    sqlite3_limit(db->sqlite, SQLITE_LIMIT_EXPR_DEPTH, 100);
    sqlite3_limit(db->sqlite, SQLITE_LIMIT_VARIABLE_NUMBER, 128);
    sqlite3_limit(db->sqlite, SQLITE_LIMIT_VDBE_OP, 100000);
    sqlite3_set_authorizer(db->sqlite, pdb_authorizer, db);
    sqlite3_progress_handler(db->sqlite, 1000, pdb_progress, db);
    rc = pdb_exec_internal(db,
            "PRAGMA page_size=4096;"
            "PRAGMA journal_mode=DELETE;"
            "PRAGMA synchronous=FULL;"
            "PRAGMA foreign_keys=ON;"
            "PRAGMA temp_store=MEMORY;"
            "PRAGMA max_page_count=4096;");
    if (rc == PDB_OK) {
        rc = pdb_open_metadata(db);
    }
    if (rc != PDB_OK) {
        PDb_Close(db);
        return rc;
    }
    *outDb = db;
    return PDB_OK;
}

PDB_API void PDb_Close(PDbHandle db)
{
    PDbStmtHandle statement;

    if (db == NULL) {
        return;
    }
    while (db->statements != NULL) {
        statement = db->statements;
        db->statements = statement->next;
        if (statement->sqlite_stmt != NULL) {
            sqlite3_finalize(statement->sqlite_stmt);
        }
        free(statement);
    }
    if (db->sqlite != NULL) {
        sqlite3_progress_handler(db->sqlite, 0, NULL, NULL);
        sqlite3_close(db->sqlite);
    }
    free(db);
}

PDB_API int PDb_Prepare(PDbHandle db, const char* sql,
        PDbStmtHandle* outStmt)
{
    PDbStmtHandle statement;
    int rc;

    if (db == NULL || db->sqlite == NULL || sql == NULL || outStmt == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    *outStmt = NULL;
    statement = (PDbStmtHandle)malloc(sizeof(*statement));
    if (statement == NULL) {
        return PDB_NOMEM;
    }
    memset(statement, 0, sizeof(*statement));
    rc = pdb_prepare_user(db, sql, &statement->sqlite_stmt);
    if (rc != PDB_OK) {
        free(statement);
        return rc;
    }
    statement->owner = db;
    statement->next = db->statements;
    db->statements = statement;
    *outStmt = statement;
    return PDB_OK;
}

PDB_API int PDb_Exec(PDbHandle db, const char* sql)
{
    PDbStmtHandle statement;
    int rc;

    if (db == NULL || sql == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    if ((int)strlen(sql) > PDB_SQL_MAX_BYTES) {
        pdb_set_error(db, "SQL length exceeds the Positron DB limit");
        return PDB_LIMIT;
    }
    if (db->mode == PDB_OPEN_LOCAL_FULL_SQL) {
        return pdb_exec_internal(db, sql);
    }
    rc = PDb_Prepare(db, sql, &statement);
    if (rc != PDB_OK) {
        return rc;
    }
    for (;;) {
        rc = PDb_Step(statement);
        if (rc == PDB_STEP_ROW) {
            continue;
        }
        break;
    }
    PDb_Finalize(statement);
    return rc == PDB_STEP_DONE ? PDB_OK : rc;
}

PDB_API int PDb_BindNull(PDbStmtHandle stmt, int index)
{
    int rc;

    if (stmt == NULL || stmt->sqlite_stmt == NULL || index <= 0) {
        return PDB_INVALID_ARGUMENT;
    }
    rc = sqlite3_bind_null(stmt->sqlite_stmt, index);
    return rc == SQLITE_OK ? PDB_OK : pdb_set_sqlite_error(stmt->owner, rc);
}

PDB_API int PDb_BindInt64(PDbStmtHandle stmt, int index, __int64 value)
{
    int rc;

    if (stmt == NULL || stmt->sqlite_stmt == NULL || index <= 0) {
        return PDB_INVALID_ARGUMENT;
    }
    rc = sqlite3_bind_int64(stmt->sqlite_stmt, index, value);
    return rc == SQLITE_OK ? PDB_OK : pdb_set_sqlite_error(stmt->owner, rc);
}

PDB_API int PDb_BindDouble(PDbStmtHandle stmt, int index, double value)
{
    int rc;

    if (stmt == NULL || stmt->sqlite_stmt == NULL || index <= 0) {
        return PDB_INVALID_ARGUMENT;
    }
    rc = sqlite3_bind_double(stmt->sqlite_stmt, index, value);
    return rc == SQLITE_OK ? PDB_OK : pdb_set_sqlite_error(stmt->owner, rc);
}

PDB_API int PDb_BindText(PDbStmtHandle stmt, int index,
        const char* text, int length)
{
    int rc;

    if (stmt == NULL || stmt->sqlite_stmt == NULL || index <= 0 ||
            text == NULL || length < 0 || length > PDB_SYNC_MAX_BODY_BYTES) {
        return PDB_INVALID_ARGUMENT;
    }
    rc = sqlite3_bind_text(stmt->sqlite_stmt, index, text, length,
            SQLITE_TRANSIENT);
    return rc == SQLITE_OK ? PDB_OK : pdb_set_sqlite_error(stmt->owner, rc);
}

PDB_API int PDb_BindBlob(PDbStmtHandle stmt, int index,
        const void* data, int length)
{
    int rc;

    if (stmt == NULL || stmt->sqlite_stmt == NULL || index <= 0 ||
            length < 0 || length > PDB_SYNC_MAX_BODY_BYTES ||
            (data == NULL && length > 0)) {
        return PDB_INVALID_ARGUMENT;
    }
    rc = sqlite3_bind_blob(stmt->sqlite_stmt, index, data, length,
            SQLITE_TRANSIENT);
    return rc == SQLITE_OK ? PDB_OK : pdb_set_sqlite_error(stmt->owner, rc);
}

PDB_API int PDb_Step(PDbStmtHandle stmt)
{
    int rc;
    int drain_rc;

    if (stmt == NULL || stmt->sqlite_stmt == NULL || stmt->owner == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    if (stmt->complete) {
        return PDB_STEP_DONE;
    }
    rc = sqlite3_step(stmt->sqlite_stmt);
    if (rc == SQLITE_ROW) {
        return PDB_STEP_ROW;
    }
    if (rc == SQLITE_DONE) {
        stmt->complete = 1;
        drain_rc = pdb_drain_dirty(stmt->owner);
        stmt->owner->cancelled = 0;
        return drain_rc == PDB_OK ? PDB_STEP_DONE : drain_rc;
    }
    if (rc == SQLITE_INTERRUPT) {
        stmt->owner->cancelled = 0;
    }
    return pdb_set_sqlite_error(stmt->owner, rc);
}

PDB_API int PDb_Finalize(PDbStmtHandle stmt)
{
    PDbStmtHandle cursor;
    PDbStmtHandle previous;
    PDbHandle owner;
    int rc;

    if (stmt == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    owner = stmt->owner;
    if (owner != NULL) {
        previous = NULL;
        cursor = owner->statements;
        while (cursor != NULL && cursor != stmt) {
            previous = cursor;
            cursor = cursor->next;
        }
        if (cursor == stmt) {
            if (previous == NULL) {
                owner->statements = stmt->next;
            } else {
                previous->next = stmt->next;
            }
        }
    }
    rc = SQLITE_OK;
    if (stmt->sqlite_stmt != NULL) {
        rc = sqlite3_finalize(stmt->sqlite_stmt);
    }
    free(stmt);
    if (rc != SQLITE_OK) {
        return PDB_ERROR;
    }
    return PDB_OK;
}

PDB_API int PDb_ColumnCount(PDbStmtHandle stmt)
{
    if (stmt == NULL || stmt->sqlite_stmt == NULL) {
        return 0;
    }
    return sqlite3_column_count(stmt->sqlite_stmt);
}

PDB_API int PDb_ColumnType(PDbStmtHandle stmt, int index)
{
    int type;

    if (stmt == NULL || stmt->sqlite_stmt == NULL || index < 0 ||
            index >= sqlite3_column_count(stmt->sqlite_stmt)) {
        return PDB_VALUE_NULL;
    }
    type = sqlite3_column_type(stmt->sqlite_stmt, index);
    if (type == SQLITE_INTEGER) {
        return PDB_VALUE_INTEGER;
    }
    if (type == SQLITE_FLOAT) {
        return PDB_VALUE_REAL;
    }
    if (type == SQLITE_TEXT) {
        return PDB_VALUE_TEXT;
    }
    if (type == SQLITE_BLOB) {
        return PDB_VALUE_BLOB;
    }
    return PDB_VALUE_NULL;
}

PDB_API __int64 PDb_ColumnInt64(PDbStmtHandle stmt, int index)
{
    if (stmt == NULL || stmt->sqlite_stmt == NULL || index < 0 ||
            index >= sqlite3_column_count(stmt->sqlite_stmt)) {
        return 0;
    }
    return sqlite3_column_int64(stmt->sqlite_stmt, index);
}

PDB_API double PDb_ColumnDouble(PDbStmtHandle stmt, int index)
{
    if (stmt == NULL || stmt->sqlite_stmt == NULL || index < 0 ||
            index >= sqlite3_column_count(stmt->sqlite_stmt)) {
        return 0.0;
    }
    return sqlite3_column_double(stmt->sqlite_stmt, index);
}

PDB_API const char* PDb_ColumnText(PDbStmtHandle stmt, int index)
{
    if (stmt == NULL || stmt->sqlite_stmt == NULL || index < 0 ||
            index >= sqlite3_column_count(stmt->sqlite_stmt)) {
        return NULL;
    }
    return (const char*)sqlite3_column_text(stmt->sqlite_stmt, index);
}

PDB_API const void* PDb_ColumnBlob(PDbStmtHandle stmt, int index)
{
    if (stmt == NULL || stmt->sqlite_stmt == NULL || index < 0 ||
            index >= sqlite3_column_count(stmt->sqlite_stmt)) {
        return NULL;
    }
    return sqlite3_column_blob(stmt->sqlite_stmt, index);
}

PDB_API int PDb_ColumnBytes(PDbStmtHandle stmt, int index)
{
    if (stmt == NULL || stmt->sqlite_stmt == NULL || index < 0 ||
            index >= sqlite3_column_count(stmt->sqlite_stmt)) {
        return 0;
    }
    return sqlite3_column_bytes(stmt->sqlite_stmt, index);
}

PDB_API int PDb_Begin(PDbHandle db)
{
    int rc;

    if (db == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    if (db->in_transaction) {
        return PDB_STATE;
    }
    rc = pdb_exec_internal(db, "BEGIN IMMEDIATE");
    if (rc == PDB_OK) {
        db->in_transaction = 1;
    }
    return rc;
}

PDB_API int PDb_Commit(PDbHandle db)
{
    int rc;

    if (db == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    if (!db->in_transaction) {
        return PDB_STATE;
    }
    rc = pdb_drain_dirty(db);
    if (rc != PDB_OK) {
        pdb_exec_internal(db, "ROLLBACK");
        db->in_transaction = 0;
        return rc;
    }
    rc = pdb_exec_internal(db, "COMMIT");
    if (rc == PDB_OK) {
        db->in_transaction = 0;
    }
    return rc;
}

PDB_API int PDb_Rollback(PDbHandle db)
{
    int rc;

    if (db == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    if (!db->in_transaction) {
        return PDB_STATE;
    }
    rc = pdb_exec_internal(db, "ROLLBACK");
    db->in_transaction = 0;
    return rc;
}

PDB_API int PDb_Cancel(PDbHandle db)
{
    if (db == NULL || db->sqlite == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    db->cancelled = 1;
    sqlite3_interrupt(db->sqlite);
    return PDB_OK;
}

PDB_API int PDb_GetLastError(PDbHandle db, char* buffer, int capacity)
{
    int length;

    if (db == NULL || buffer == NULL || capacity <= 0) {
        return PDB_INVALID_ARGUMENT;
    }
    length = (int)strlen(db->last_error);
    if (capacity <= length) {
        return PDB_BUFFER_TOO_SMALL;
    }
    memcpy(buffer, db->last_error, (size_t)length);
    buffer[length] = '\0';
    return PDB_OK;
}

PDB_API int PDb_CopyLastError(PDbHandle db, char* buffer, int capacity)
{
    return PDb_GetLastError(db, buffer, capacity);
}

PDB_API int PDb_ApplyMigration(PDbHandle db, int version,
        const char* sql_script)
{
    int current;
    int index;
    int rc;

    if (db == NULL || sql_script == NULL || version < 0 ||
            (int)strlen(sql_script) > PDB_SYNC_MAX_BODY_BYTES) {
        return PDB_INVALID_ARGUMENT;
    }
    current = pdb_meta_get_int(db, "schema_version", 0);
    if (version < current) {
        pdb_set_error(db, "migration version is older than the database");
        return PDB_STATE;
    }
    if (version == current && sql_script[0] == '\0') {
        return PDB_OK;
    }
    if (version == current) {
        pdb_set_error(db, "migration version has already been applied");
        return PDB_STATE;
    }
    if (db->in_transaction) {
        return PDB_STATE;
    }
    rc = PDb_Begin(db);
    if (rc != PDB_OK) {
        return rc;
    }
    db->migration = 1;
    rc = pdb_exec_internal(db, sql_script);
    db->migration = 0;
    if (rc == PDB_OK && db->mode == PDB_OPEN_SYNC) {
        for (index = 0; index < db->table_count; ++index) {
            rc = pdb_validate_table_exists(db,
                    db->tables[index].table_name);
            if (rc == PDB_OK) {
                rc = pdb_validate_table_schema(db, &db->tables[index]);
            }
            if (rc == PDB_OK) {
                rc = pdb_install_triggers(db, &db->tables[index]);
            }
            if (rc != PDB_OK) {
                break;
            }
        }
    }
    if (rc == PDB_OK) {
        rc = pdb_meta_set_int(db, "schema_version", version);
    }
    if (rc == PDB_OK) {
        rc = PDb_Commit(db);
    } else {
        PDb_Rollback(db);
    }
    return rc;
}

PDB_API int PDb_SyncConfigure(PDbHandle db, const char* client_id,
        int schema_version, const char* schema_hash)
{
    int stored_schema_version;
    int rc;

    if (db == NULL || db->mode != PDB_OPEN_SYNC || client_id == NULL ||
            schema_hash == NULL || client_id[0] == '\0' ||
            schema_hash[0] == '\0' || schema_version < 0 ||
            (int)strlen(client_id) >= PDB_SYNC_CLIENT_MAX ||
            (int)strlen(schema_hash) >= PDB_SYNC_NAME_MAX) {
        return PDB_INVALID_ARGUMENT;
    }
    stored_schema_version = pdb_meta_get_int(db, "schema_version", 0);
    if (stored_schema_version != schema_version) {
        pdb_set_error(db, "sync configuration does not match migration version");
        return PDB_SCHEMA_MISMATCH;
    }
    rc = pdb_meta_set(db, "client_id", client_id);
    if (rc == PDB_OK) {
        rc = pdb_meta_set(db, "schema_hash", schema_hash);
    }
    return rc;
}

PDB_API int PDb_SyncRegisterTable(PDbHandle db, const char* table_name,
        const char* primary_key, const PDbSyncColumn* columns,
        int column_count)
{
    PDbTableInfo table;
    PDbTableInfo candidate;
    int index;
    int key_found;
    int existing;
    int rc;

    if (db == NULL || db->mode != PDB_OPEN_SYNC ||
            !pdb_is_identifier(table_name) ||
            !pdb_is_identifier(primary_key) ||
            pdb_is_reserved_name(table_name) ||
            pdb_is_reserved_name(primary_key) || columns == NULL ||
            column_count <= 0 || column_count > PDB_SYNC_MAX_COLUMNS) {
        return PDB_INVALID_ARGUMENT;
    }
    existing = pdb_find_table(db, table_name);
    if (existing >= 0) {
        if (strcmp(db->tables[existing].primary_key, primary_key) != 0 ||
                db->tables[existing].column_count != column_count) {
            return PDB_STATE;
        }
        candidate = db->tables[existing];
        for (index = 0; index < column_count; ++index) {
            if (columns[index].name == NULL ||
                    strcmp(db->tables[existing].columns[index],
                    columns[index].name) != 0) {
                return PDB_STATE;
            }
            if (columns[index].value_type < PDB_VALUE_NULL ||
                    columns[index].value_type > PDB_VALUE_BLOB) {
                return PDB_INVALID_ARGUMENT;
            }
            candidate.column_types[index] = columns[index].value_type;
        }
        return pdb_validate_table_schema(db, &candidate);
    }
    if (db->table_count >= PDB_SYNC_MAX_TABLES) {
        return PDB_LIMIT;
    }
    rc = pdb_validate_table_exists(db, table_name);
    if (rc != PDB_OK) {
        return rc;
    }
    memset(&table, 0, sizeof(table));
    table.used = 1;
    pdb_copy_text(table.table_name, sizeof(table.table_name), table_name);
    pdb_copy_text(table.primary_key, sizeof(table.primary_key), primary_key);
    table.column_count = column_count;
    key_found = 0;
    for (index = 0; index < column_count; ++index) {
        if (!pdb_is_identifier(columns[index].name) ||
                pdb_is_reserved_name(columns[index].name)) {
            return PDB_INVALID_ARGUMENT;
        }
        if (columns[index].value_type < PDB_VALUE_NULL ||
                columns[index].value_type > PDB_VALUE_BLOB) {
            return PDB_INVALID_ARGUMENT;
        }
        if (index > 0) {
            int previous;
            for (previous = 0; previous < index; ++previous) {
                if (strcmp(columns[previous].name, columns[index].name) == 0) {
                    return PDB_INVALID_ARGUMENT;
                }
            }
        }
        if (strcmp(columns[index].name, primary_key) == 0) {
            key_found = 1;
        }
        pdb_copy_text(table.columns[index], sizeof(table.columns[index]),
                columns[index].name);
        table.column_types[index] = columns[index].value_type;
    }
    if (!key_found) {
        pdb_set_error(db, "sync primary key must be in the column list");
        return PDB_INVALID_ARGUMENT;
    }
    rc = pdb_validate_table_schema(db, &table);
    if (rc != PDB_OK) {
        return rc;
    }
    rc = PDb_Begin(db);
    if (rc != PDB_OK) {
        return rc;
    }
    rc = pdb_store_table(db, &table);
    if (rc == PDB_OK) {
        rc = pdb_install_triggers(db, &table);
    }
    if (rc == PDB_OK) {
        db->tables[db->table_count] = table;
        db->table_count += 1;
        rc = PDb_Commit(db);
    } else {
        PDb_Rollback(db);
    }
    return rc;
}

static int pdb_build_request_alloc(PDbHandle db, char** outBody,
        int* outLength)
{
    sqlite3_stmt* stmt;
    PDbWriter writer;
    char* body;
    char client_id[PDB_SYNC_CLIENT_MAX];
    char schema_hash[PDB_SYNC_NAME_MAX];
    char cursor[PDB_SYNC_NAME_MAX];
    char op_id[PDB_SYNC_CLIENT_MAX + 32];
    const unsigned char* table_name;
    const unsigned char* row_key;
    const unsigned char* base_version;
    const unsigned char* row_json;
    char sequence_text[32];
    __int64 sequence;
    __int64 cursor_value;
    int schema_version;
    int rc;
    int push_count;
    int push_more;

    if (db == NULL || outBody == NULL || outLength == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    *outBody = NULL;
    *outLength = 0;
    if (db->mode != PDB_OPEN_SYNC) {
        return PDB_STATE;
    }
    if (db->in_transaction) {
        pdb_set_error(db, "cannot build a sync request during a transaction");
        return PDB_STATE;
    }
    rc = pdb_drain_dirty(db);
    if (rc != PDB_OK) {
        return rc;
    }
    body = (char*)malloc(PDB_SYNC_MAX_BODY_BYTES);
    if (body == NULL) {
        return PDB_NOMEM;
    }
    pdb_writer_init(&writer, body, PDB_SYNC_MAX_BODY_BYTES);
    if (pdb_meta_get(db, "client_id", client_id, sizeof(client_id)) !=
            PDB_OK ||
            pdb_meta_get(db, "schema_hash", schema_hash,
            sizeof(schema_hash)) != PDB_OK ||
            client_id[0] == '\0' || schema_hash[0] == '\0') {
        free(body);
        pdb_set_error(db, "sync database is not configured");
        return PDB_STATE;
    }
    if (pdb_meta_get(db, "cursor", cursor, sizeof(cursor)) != PDB_OK) {
        pdb_copy_text(cursor, sizeof(cursor), "0");
    }
    if (!pdb_parse_version(cursor, &cursor_value)) {
        free(body);
        pdb_set_error(db, "sync cursor is invalid");
        return PDB_STATE;
    }
    schema_version = pdb_meta_get_int(db, "schema_version", 0);
    pdb_writer_literal(&writer, "{\"protocol\":1,\"client_id\":");
    pdb_writer_json_string(&writer, client_id);
    pdb_writer_literal(&writer, ",\"schema_version\":");
    pdb_writer_int64(&writer, schema_version);
    pdb_writer_literal(&writer, ",\"schema_hash\":");
    pdb_writer_json_string(&writer, schema_hash);
    pdb_writer_literal(&writer, ",\"cursor\":");
    pdb_writer_json_string(&writer, cursor);
    pdb_writer_literal(&writer, ",\"push\":[");

    rc = pdb_prepare_internal(db,
            "SELECT sequence,table_name,row_key,action,base_version,row_json "
            "FROM __pdb_outbox ORDER BY sequence LIMIT 65", &stmt);
    if (rc != PDB_OK) {
        free(body);
        return rc;
    }
    push_count = 0;
    push_more = 0;
    while ((rc = pdb_step_internal(db, stmt)) == SQLITE_ROW) {
        if (push_count >= PDB_SYNC_PULL_LIMIT) {
            push_more = 1;
            continue;
        }
        sequence = sqlite3_column_int64(stmt, 0);
        _snprintf(sequence_text, sizeof(sequence_text) - 1,
                "%I64d", sequence);
        sequence_text[sizeof(sequence_text) - 1] = '\0';
        _snprintf(op_id, sizeof(op_id) - 1, "%s:%s",
                client_id, sequence_text);
        op_id[sizeof(op_id) - 1] = '\0';
        table_name = sqlite3_column_text(stmt, 1);
        row_key = sqlite3_column_text(stmt, 2);
        base_version = sqlite3_column_text(stmt, 4);
        row_json = sqlite3_column_text(stmt, 5);
        if (push_count != 0) {
            pdb_writer_char(&writer, ',');
        }
        pdb_writer_literal(&writer, "{\"op_id\":");
        pdb_writer_json_string(&writer, op_id);
        pdb_writer_literal(&writer, ",\"entity\":");
        pdb_writer_json_string(&writer, (const char*)table_name);
        pdb_writer_literal(&writer, ",\"key\":");
        pdb_writer_json_string(&writer, (const char*)row_key);
        pdb_writer_literal(&writer, ",\"action\":");
        pdb_writer_json_string(&writer,
                sqlite3_column_int(stmt, 3) == PDB_DIRTY_DELETE ?
                "delete" : "upsert");
        pdb_writer_literal(&writer, ",\"base_version\":");
        pdb_writer_json_string(&writer, (const char*)base_version);
        pdb_writer_literal(&writer, ",\"values\":");
        if (row_json == NULL) {
            pdb_writer_literal(&writer, "null");
        } else {
            pdb_writer_put(&writer, (const char*)row_json,
                    (int)strlen((const char*)row_json));
        }
        pdb_writer_char(&writer, '}');
        push_count += 1;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        free(body);
        return pdb_set_sqlite_error(db, rc);
    }
    pdb_writer_literal(&writer, "],\"push_more\":");
    pdb_writer_literal(&writer, push_more ? "true" : "false");
    pdb_writer_literal(&writer, ",\"pull_limit\":64}");
    if (writer.overflow || writer.length >= PDB_SYNC_MAX_BODY_BYTES) {
        free(body);
        pdb_set_error(db, "sync request exceeds the HTTP body budget");
        return PDB_LIMIT;
    }
    *outBody = body;
    *outLength = writer.length;
    return PDB_OK;
}

PDB_API int PDb_SyncBuildRequest(PDbHandle db, char* buffer, int capacity,
        int* outLength)
{
    char* body;
    int length;
    int rc;

    if (db == NULL || outLength == NULL || capacity < 0 ||
            (buffer == NULL && capacity != 0)) {
        return PDB_INVALID_ARGUMENT;
    }
    rc = pdb_build_request_alloc(db, &body, &length);
    if (rc != PDB_OK) {
        return rc;
    }
    *outLength = length;
    if (buffer == NULL || capacity < length + 1) {
        free(body);
        return buffer == NULL && capacity == 0 ? PDB_OK :
                PDB_BUFFER_TOO_SMALL;
    }
    memcpy(buffer, body, (size_t)length + 1);
    free(body);
    return PDB_OK;
}

static int pdb_parse_op_sequence(PDbHandle db, const char* op_id,
        __int64* outSequence)
{
    char client_id[PDB_SYNC_CLIENT_MAX];
    const char* separator;
    int client_length;

    if (db == NULL || op_id == NULL || outSequence == NULL ||
            pdb_meta_get(db, "client_id", client_id,
            sizeof(client_id)) != PDB_OK) {
        return 0;
    }
    separator = strrchr(op_id, ':');
    client_length = (int)strlen(client_id);
    if (separator == NULL || separator == op_id ||
            (int)(separator - op_id) != client_length ||
            memcmp(op_id, client_id, (size_t)client_length) != 0 ||
            separator[1] == '\0') {
        return 0;
    }
    if (!pdb_parse_int64(separator + 1, outSequence) ||
            *outSequence <= 0) {
        return 0;
    }
    return 1;
}

static int pdb_copy_outbox_sequence(PDbHandle db, __int64 sequence,
        char* table_name, int table_capacity, char* row_key,
        int key_capacity, char** outRowJson, int* outAction,
        char* base_version, int base_capacity)
{
    sqlite3_stmt* stmt;
    const unsigned char* text;
    int bytes;
    int rc;

    if (table_name == NULL || row_key == NULL || outRowJson == NULL ||
            outAction == NULL || base_version == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    *outRowJson = NULL;
    *outAction = 0;
    table_name[0] = '\0';
    row_key[0] = '\0';
    base_version[0] = '\0';
    rc = pdb_prepare_internal(db,
            "SELECT table_name,row_key,action,base_version,row_json "
            "FROM __pdb_outbox WHERE sequence=?1", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_int64(stmt, 1, sequence);
    rc = pdb_step_internal(db, stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return PDB_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return pdb_set_sqlite_error(db, rc);
    }
    text = sqlite3_column_text(stmt, 0);
    pdb_copy_text(table_name, table_capacity, (const char*)text);
    text = sqlite3_column_text(stmt, 1);
    pdb_copy_text(row_key, key_capacity, (const char*)text);
    *outAction = sqlite3_column_int(stmt, 2);
    text = sqlite3_column_text(stmt, 3);
    pdb_copy_text(base_version, base_capacity, (const char*)text);
    text = sqlite3_column_text(stmt, 4);
    if (text != NULL) {
        bytes = sqlite3_column_bytes(stmt, 4);
        *outRowJson = (char*)malloc((size_t)bytes + 1);
        if (*outRowJson == NULL) {
            sqlite3_finalize(stmt);
            return PDB_NOMEM;
        }
        memcpy(*outRowJson, text, (size_t)bytes);
        (*outRowJson)[bytes] = '\0';
    }
    sqlite3_finalize(stmt);
    return PDB_OK;
}

static int pdb_delete_outbox_sequence(PDbHandle db, __int64 sequence)
{
    sqlite3_stmt* stmt;
    int rc;

    rc = pdb_prepare_internal(db,
            "DELETE FROM __pdb_outbox WHERE sequence=?1", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_int64(stmt, 1, sequence);
    rc = pdb_step_internal(db, stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return pdb_set_sqlite_error(db, rc);
    }
    return PDB_OK;
}

static int pdb_delete_outbox_row(PDbHandle db, const char* table_name,
        const char* row_key)
{
    sqlite3_stmt* stmt;
    int rc;

    rc = pdb_prepare_internal(db,
            "DELETE FROM __pdb_outbox WHERE table_name=?1 AND row_key=?2",
            &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, row_key, -1, SQLITE_TRANSIENT);
    rc = pdb_step_internal(db, stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return pdb_set_sqlite_error(db, rc);
    }
    return PDB_OK;
}

static int pdb_bind_json_value(PDbHandle db, sqlite3_stmt* stmt, int index,
        HANDLE value)
{
    HANDLE json_value;
    const char* type;
    const char* text;
    __int64 integer;
    double real;
    unsigned char* blob;
    int encoded_length;
    int blob_length;
    int rc;

    if (value == NULL || PJson_GetType(value) != PJSON_TYPE_OBJECT) {
        return PDB_ERROR;
    }
    type = PJson_GetString(value, "t");
    if (type == NULL) {
        return PDB_ERROR;
    }
    if (strcmp(type, "n") == 0) {
        json_value = PJson_GetObject(value, "v");
        if (json_value == NULL || PJson_GetType(json_value) !=
                PJSON_TYPE_NULL) {
            return PDB_ERROR;
        }
        rc = sqlite3_bind_null(stmt, index);
        return rc == SQLITE_OK ? PDB_OK : pdb_set_sqlite_error(db, rc);
    }
    json_value = PJson_GetObject(value, "v");
    if (json_value == NULL || PJson_GetType(json_value) !=
            PJSON_TYPE_STRING) {
        return PDB_ERROR;
    }
    text = PJson_GetString(value, "v");
    if (text == NULL) {
        return PDB_ERROR;
    }
    if (strcmp(type, "i") == 0) {
        if (!pdb_parse_int64(text, &integer)) {
            return PDB_ERROR;
        }
        rc = sqlite3_bind_int64(stmt, index, integer);
        return rc == SQLITE_OK ? PDB_OK : pdb_set_sqlite_error(db, rc);
    }
    if (strcmp(type, "r") == 0) {
        if (!pdb_parse_double(text, &real)) {
            return PDB_ERROR;
        }
        rc = sqlite3_bind_double(stmt, index, real);
        return rc == SQLITE_OK ? PDB_OK : pdb_set_sqlite_error(db, rc);
    }
    if (strcmp(type, "s") == 0) {
        if ((int)strlen(text) > PDB_SYNC_MAX_BODY_BYTES) {
            return PDB_LIMIT;
        }
        rc = sqlite3_bind_text(stmt, index, text, -1, SQLITE_TRANSIENT);
        return rc == SQLITE_OK ? PDB_OK : pdb_set_sqlite_error(db, rc);
    }
    if (strcmp(type, "b") == 0) {
        encoded_length = (int)strlen(text);
        if (encoded_length > PDB_SYNC_MAX_BODY_BYTES) {
            return PDB_LIMIT;
        }
        blob = (unsigned char*)malloc((size_t)((encoded_length / 4) * 3 + 3));
        if (blob == NULL) {
            return PDB_NOMEM;
        }
        rc = pdb_base64_decode(text, blob,
                (encoded_length / 4) * 3 + 3, &blob_length);
        if (rc == PDB_OK) {
            rc = sqlite3_bind_blob(stmt, index, blob, blob_length,
                    SQLITE_TRANSIENT) == SQLITE_OK ? PDB_OK :
                    pdb_set_sqlite_error(db, SQLITE_ERROR);
        }
        free(blob);
        return rc;
    }
    return PDB_ERROR;
}

static int pdb_apply_row_handle(PDbHandle db, PDbTableInfo* table,
        const char* row_key, HANDLE row, int deleted)
{
    sqlite3_stmt* stmt;
    char sql[8192];
    char quoted_table[128];
    char quoted_column[128];
    HANDLE key_value;
    const char* key_type;
    const char* key_text;
    __int64 key_number;
    __int64 row_number;
    int index;
    int used;
    int rc;

    if (db == NULL || table == NULL || row_key == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    if (deleted) {
        if (!pdb_quote_identifier(quoted_table, sizeof(quoted_table),
                table->table_name) ||
                !pdb_quote_identifier(quoted_column, sizeof(quoted_column),
                table->primary_key)) {
            return PDB_INVALID_ARGUMENT;
        }
        _snprintf(sql, sizeof(sql) - 1,
                "DELETE FROM %s WHERE CAST(%s AS TEXT)=?1",
                quoted_table, quoted_column);
        sql[sizeof(sql) - 1] = '\0';
        rc = pdb_prepare_internal(db, sql, &stmt);
        if (rc != PDB_OK) {
            return rc;
        }
        sqlite3_bind_text(stmt, 1, row_key, -1, SQLITE_TRANSIENT);
        db->applying = 1;
        rc = pdb_step_internal(db, stmt);
        db->applying = 0;
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            return pdb_set_sqlite_error(db, rc);
        }
        return pdb_delete_dirty(db, table->table_name, row_key);
    }
    if (row == NULL || PJson_GetType(row) != PJSON_TYPE_OBJECT) {
        return PDB_ERROR;
    }
    key_value = PJson_GetObject(row, table->primary_key);
    key_type = PJson_GetString(key_value, "t");
    key_text = PJson_GetString(key_value, "v");
    if (key_type == NULL || key_text == NULL) {
        return PDB_ERROR;
    }
    if (strcmp(key_type, "i") == 0) {
        if (!pdb_parse_int64(key_text, &key_number) ||
                !pdb_parse_int64(row_key, &row_number) ||
                key_number != row_number) {
            return PDB_ERROR;
        }
    } else if (strcmp(key_type, "s") == 0) {
        if (strcmp(key_text, row_key) != 0) {
            return PDB_ERROR;
        }
    } else {
        return PDB_ERROR;
    }
    if (!pdb_quote_identifier(quoted_table, sizeof(quoted_table),
            table->table_name)) {
        return PDB_INVALID_ARGUMENT;
    }
    used = _snprintf(sql, sizeof(sql) - 1, "INSERT OR REPLACE INTO %s (",
            quoted_table);
    if (used < 0) {
        return PDB_LIMIT;
    }
    for (index = 0; index < table->column_count; ++index) {
        if (!pdb_quote_identifier(quoted_column, sizeof(quoted_column),
                table->columns[index])) {
            return PDB_INVALID_ARGUMENT;
        }
        used += _snprintf(sql + used, sizeof(sql) - used - 1, "%s%s",
                index == 0 ? "" : ",", quoted_column);
        if (used < 0 || used >= (int)sizeof(sql) - 1) {
            return PDB_LIMIT;
        }
    }
    used += _snprintf(sql + used, sizeof(sql) - used - 1, ") VALUES (");
    for (index = 0; index < table->column_count; ++index) {
        used += _snprintf(sql + used, sizeof(sql) - used - 1, "%s?%d",
                index == 0 ? "" : ",", index + 1);
        if (used < 0 || used >= (int)sizeof(sql) - 1) {
            return PDB_LIMIT;
        }
    }
    used += _snprintf(sql + used, sizeof(sql) - used - 1, ")");
    if (used < 0 || used >= (int)sizeof(sql) - 1) {
        return PDB_LIMIT;
    }
    rc = pdb_prepare_internal(db, sql, &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    for (index = 0; index < table->column_count; ++index) {
        rc = pdb_bind_json_value(db, stmt, index + 1,
                PJson_GetObject(row, table->columns[index]));
        if (rc != PDB_OK) {
            sqlite3_finalize(stmt);
            return rc;
        }
    }
    db->applying = 1;
    rc = pdb_step_internal(db, stmt);
    db->applying = 0;
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return pdb_set_sqlite_error(db, rc);
    }
    return pdb_delete_dirty(db, table->table_name, row_key);
}

static int pdb_apply_row_text(PDbHandle db, PDbTableInfo* table,
        const char* row_key, const char* row_json, int deleted)
{
    HANDLE root;
    int rc;

    if (deleted || row_json == NULL || strcmp(row_json, "null") == 0) {
        return pdb_apply_row_handle(db, table, row_key, NULL, 1);
    }
    root = PJson_Parse(row_json);
    if (root == NULL) {
        return PDB_ERROR;
    }
    rc = pdb_apply_row_handle(db, table, row_key, root, 0);
    PJson_Free(root);
    return rc;
}

static int pdb_json_values_string(HANDLE item, char** outJson)
{
    HANDLE null_root;
    char* json;

    if (outJson == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    *outJson = NULL;
    if (item == NULL || PJson_GetType(item) == PJSON_TYPE_NULL) {
        null_root = PJson_Parse("null");
        if (null_root == NULL) {
            return PDB_NOMEM;
        }
        json = PJson_Serialize(null_root);
        PJson_Free(null_root);
        if (json == NULL) {
            return PDB_NOMEM;
        }
        *outJson = json;
        return PDB_OK;
    }
    json = PJson_Serialize(item);
    if (json == NULL) {
        return PDB_NOMEM;
    }
    if ((int)strlen(json) > PDB_MAX_ROW_BYTES) {
        PJson_FreeString(json);
        return PDB_LIMIT;
    }
    *outJson = json;
    return PDB_OK;
}

static int pdb_insert_conflict(PDbHandle db, const char* table_name,
        const char* row_key, __int64 op_sequence,
        const char* server_version, const char* local_row_json,
        const char* server_row_json)
{
    sqlite3_stmt* stmt;
    int rc;

    rc = pdb_prepare_internal(db,
            "INSERT INTO __pdb_conflict"
            "(table_name,row_key,op_sequence,server_version,"
            "local_row_json,server_row_json)"
            "VALUES(?1,?2,?3,?4,?5,?6)", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, row_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, op_sequence);
    sqlite3_bind_text(stmt, 4, server_version, -1, SQLITE_TRANSIENT);
    if (local_row_json == NULL) {
        sqlite3_bind_null(stmt, 5);
    } else {
        sqlite3_bind_text(stmt, 5, local_row_json, -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_text(stmt, 6, server_row_json == NULL ? "null" :
            server_row_json, -1, SQLITE_TRANSIENT);
    rc = pdb_step_internal(db, stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return pdb_set_sqlite_error(db, rc);
    }
    return PDB_OK;
}

static int pdb_json_deleted(HANDLE item, int* outDeleted)
{
    HANDLE value;

    if (item == NULL || outDeleted == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    *outDeleted = 0;
    value = PJson_GetObject(item, "deleted");
    if (value == NULL) {
        return PDB_OK;
    }
    if (PJson_GetType(value) == PJSON_TYPE_TRUE) {
        *outDeleted = 1;
        return PDB_OK;
    }
    if (PJson_GetType(value) == PJSON_TYPE_FALSE) {
        return PDB_OK;
    }
    return PDB_ERROR;
}

static const char* pdb_json_version(HANDLE item)
{
    const char* value;

    value = PJson_GetString(item, "version");
    if (value == NULL) {
        value = PJson_GetString(item, "server_version");
    }
    return value;
}

static int pdb_process_accepted(PDbHandle db, HANDLE item)
{
    const char* op_id;
    const char* version;
    char table_name[PDB_SYNC_NAME_MAX];
    char row_key[PDB_SYNC_NAME_MAX];
    char base_version[PDB_SYNC_NAME_MAX];
    char* row_json;
    __int64 sequence;
    __int64 version_value;
    int action;
    int rc;

    if (item == NULL || PJson_GetType(item) != PJSON_TYPE_OBJECT) {
        return PDB_ERROR;
    }
    op_id = PJson_GetString(item, "op_id");
    version = pdb_json_version(item);
    if (!pdb_parse_op_sequence(db, op_id, &sequence) ||
            !pdb_parse_version(version, &version_value) ||
            (int)strlen(version) >= PDB_SYNC_NAME_MAX) {
        return PDB_ERROR;
    }
    row_json = NULL;
    rc = pdb_copy_outbox_sequence(db, sequence, table_name,
            sizeof(table_name), row_key, sizeof(row_key), &row_json,
            &action, base_version, sizeof(base_version));
    if (rc == PDB_NOT_FOUND) {
        return PDB_OK;
    }
    if (rc != PDB_OK) {
        return rc;
    }
    rc = pdb_delete_outbox_sequence(db, sequence);
    if (rc == PDB_OK) {
        rc = pdb_set_row_state(db, table_name, row_key, version,
                action == PDB_DIRTY_DELETE);
    }
    if (row_json != NULL) {
        free(row_json);
    }
    return rc;
}

static int pdb_process_conflict_response(PDbHandle db, HANDLE item)
{
    const char* op_id;
    const char* version;
    const char* entity;
    const char* key;
    HANDLE values;
    char table_name[PDB_SYNC_NAME_MAX];
    char row_key[PDB_SYNC_NAME_MAX];
    char base_version[PDB_SYNC_NAME_MAX];
    char* local_json;
    char* server_json;
    PDbTableInfo* table;
    __int64 sequence;
    __int64 version_value;
    int action;
    int deleted;
    int rc;

    if (item == NULL || PJson_GetType(item) != PJSON_TYPE_OBJECT) {
        return PDB_ERROR;
    }
    op_id = PJson_GetString(item, "op_id");
    version = pdb_json_version(item);
    if (!pdb_parse_op_sequence(db, op_id, &sequence) ||
            !pdb_parse_version(version, &version_value) ||
            (int)strlen(version) >= PDB_SYNC_NAME_MAX) {
        return PDB_ERROR;
    }
    local_json = NULL;
    rc = pdb_copy_outbox_sequence(db, sequence, table_name,
            sizeof(table_name), row_key, sizeof(row_key), &local_json,
            &action, base_version, sizeof(base_version));
    if (rc == PDB_NOT_FOUND) {
        return PDB_OK;
    }
    if (rc != PDB_OK) {
        return rc;
    }
    entity = PJson_GetString(item, "entity");
    key = PJson_GetString(item, "key");
    if (entity != NULL) {
        if (!pdb_is_identifier(entity) || strcmp(table_name, entity) != 0) {
            if (local_json != NULL) {
                free(local_json);
            }
            pdb_set_error(db, "sync conflict entity does not match outbox");
            return PDB_ERROR;
        }
    }
    if (key != NULL) {
        if ((int)strlen(key) >= PDB_SYNC_NAME_MAX ||
                strcmp(row_key, key) != 0) {
            if (local_json != NULL) {
                free(local_json);
            }
            pdb_set_error(db, "sync conflict key does not match outbox");
            return PDB_ERROR;
        }
    }
    table = pdb_find_table(db, table_name) >= 0 ?
            &db->tables[pdb_find_table(db, table_name)] : NULL;
    if (table == NULL) {
        if (local_json != NULL) {
            free(local_json);
        }
        return PDB_NOT_FOUND;
    }
    values = PJson_GetObject(item, "values");
    rc = pdb_json_deleted(item, &deleted);
    if (rc != PDB_OK) {
        free(local_json);
        return rc;
    }
    if (values != NULL && PJson_GetType(values) == PJSON_TYPE_NULL) {
        deleted = 1;
    }
    if ((!deleted && (values == NULL || PJson_GetType(values) !=
            PJSON_TYPE_OBJECT)) ||
            (deleted && values != NULL && PJson_GetType(values) !=
            PJSON_TYPE_NULL)) {
        free(local_json);
        return PDB_ERROR;
    }
    server_json = NULL;
    rc = pdb_json_values_string(values, &server_json);
    if (rc == PDB_OK) {
        rc = pdb_insert_conflict(db, table_name, row_key, sequence,
                version, local_json, server_json);
    }
    if (rc == PDB_OK) {
        rc = pdb_apply_row_handle(db, table, row_key, values, deleted);
    }
    if (rc == PDB_OK) {
        rc = pdb_set_row_state(db, table_name, row_key, version, deleted);
    }
    if (rc == PDB_OK) {
        rc = pdb_delete_outbox_sequence(db, sequence);
    }
    if (local_json != NULL) {
        free(local_json);
    }
    if (server_json != NULL) {
        PJson_FreeString(server_json);
    }
    return rc;
}

static int pdb_process_change(PDbHandle db, HANDLE item)
{
    const char* entity;
    const char* key;
    const char* version;
    HANDLE values;
    PDbTableInfo* table;
    char base_version[PDB_SYNC_NAME_MAX];
    char local_table[PDB_SYNC_NAME_MAX];
    char local_key[PDB_SYNC_NAME_MAX];
    char local_base[PDB_SYNC_NAME_MAX];
    char* local_json;
    char* server_json;
    __int64 pending_sequence;
    __int64 version_value;
    int pending_action;
    int deleted;
    int rc;

    if (item == NULL || PJson_GetType(item) != PJSON_TYPE_OBJECT) {
        return PDB_ERROR;
    }
    entity = PJson_GetString(item, "entity");
    key = PJson_GetString(item, "key");
    version = pdb_json_version(item);
    values = PJson_GetObject(item, "values");
    if (entity == NULL || key == NULL ||
            !pdb_parse_version(version, &version_value) ||
            (int)strlen(entity) >= PDB_SYNC_NAME_MAX ||
            (int)strlen(key) >= PDB_SYNC_NAME_MAX ||
            (int)strlen(version) >= PDB_SYNC_NAME_MAX) {
        return PDB_ERROR;
    }
    table = pdb_find_table(db, entity) >= 0 ?
            &db->tables[pdb_find_table(db, entity)] : NULL;
    if (table == NULL) {
        return PDB_NOT_FOUND;
    }
    rc = pdb_json_deleted(item, &deleted);
    if (rc != PDB_OK) {
        return rc;
    }
    if (values != NULL && PJson_GetType(values) == PJSON_TYPE_NULL) {
        deleted = 1;
    }
    if ((!deleted && (values == NULL || PJson_GetType(values) !=
            PJSON_TYPE_OBJECT)) ||
            (deleted && values != NULL && PJson_GetType(values) !=
            PJSON_TYPE_NULL)) {
        return PDB_ERROR;
    }
    pending_sequence = 0;
    rc = pdb_find_pending(db, entity, key, &pending_sequence,
            base_version, sizeof(base_version));
    if (rc != PDB_OK && rc != PDB_NOT_FOUND) {
        return rc;
    }
    if (rc == PDB_OK) {
        local_json = NULL;
        rc = pdb_copy_outbox_sequence(db, pending_sequence,
                local_table, sizeof(local_table), local_key,
                sizeof(local_key), &local_json, &pending_action,
                local_base, sizeof(local_base));
        if (rc != PDB_OK) {
            return rc;
        }
        server_json = NULL;
        rc = pdb_json_values_string(values, &server_json);
        if (rc == PDB_OK) {
            rc = pdb_insert_conflict(db, entity, key, pending_sequence,
                    version, local_json, server_json);
        }
        if (rc == PDB_OK) {
            rc = pdb_apply_row_handle(db, table, key, values, deleted);
        }
        if (rc == PDB_OK) {
            rc = pdb_set_row_state(db, entity, key, version, deleted);
        }
        if (rc == PDB_OK) {
            rc = pdb_delete_outbox_sequence(db, pending_sequence);
        }
        if (local_json != NULL) {
            free(local_json);
        }
        if (server_json != NULL) {
            PJson_FreeString(server_json);
        }
        return rc;
    }
    rc = pdb_apply_row_handle(db, table, key, values, deleted);
    if (rc == PDB_OK) {
        rc = pdb_set_row_state(db, entity, key, version, deleted);
    }
    return rc;
}

static int pdb_process_response_array(PDbHandle db, HANDLE array,
        int kind)
{
    int index;
    int count;
    HANDLE item;
    int rc;

    if (array == NULL) {
        return PDB_OK;
    }
    if (PJson_GetType(array) != PJSON_TYPE_ARRAY) {
        return PDB_ERROR;
    }
    count = PJson_GetArraySize(array);
    if (count > PDB_SYNC_PULL_LIMIT * 4) {
        return PDB_LIMIT;
    }
    for (index = 0; index < count; ++index) {
        item = PJson_GetArrayItem(array, index);
        if (kind == 1) {
            rc = pdb_process_accepted(db, item);
        } else if (kind == 2) {
            rc = pdb_process_conflict_response(db, item);
        } else {
            rc = pdb_process_change(db, item);
        }
        if (rc != PDB_OK) {
            return rc;
        }
    }
    return PDB_OK;
}

PDB_API int PDb_SyncApplyResponse(PDbHandle db, int http_status,
        const char* body, int body_length)
{
    char* input;
    char expected_hash[PDB_SYNC_NAME_MAX];
    char stored_cursor[PDB_SYNC_NAME_MAX];
    const char* response_hash;
    const char* next_cursor;
    HANDLE root;
    HANDLE accepted;
    HANDLE conflicts;
    HANDLE changes;
    HANDLE schema_version_node;
    int expected_schema_version;
    int response_schema_version;
    __int64 next_cursor_value;
    __int64 stored_cursor_value;
    double response_schema_value;
    int rc;

    if (db == NULL || db->mode != PDB_OPEN_SYNC) {
        return PDB_INVALID_ARGUMENT;
    }
    if (http_status < 200 || http_status >= 300) {
        pdb_set_error(db, "HTTP sync response was not successful");
        return PDB_NETWORK;
    }
    if (body == NULL || body_length <= 0) {
        return PDB_INVALID_ARGUMENT;
    }
    if (body_length > PDB_SYNC_MAX_BODY_BYTES) {
        pdb_set_error(db, "HTTP sync response exceeds the body budget");
        return PDB_LIMIT;
    }
    input = (char*)malloc((size_t)body_length + 1);
    if (input == NULL) {
        return PDB_NOMEM;
    }
    memcpy(input, body, (size_t)body_length);
    input[body_length] = '\0';
    root = PJson_Parse(input);
    free(input);
    if (root == NULL || PJson_GetType(root) != PJSON_TYPE_OBJECT) {
        if (root != NULL) {
            PJson_Free(root);
        }
        pdb_set_error(db, "invalid JSON sync response");
        return PDB_ERROR;
    }
    response_hash = PJson_GetString(root, "schema_hash");
    if (response_hash == NULL ||
            pdb_meta_get(db, "schema_hash", expected_hash,
            sizeof(expected_hash)) != PDB_OK ||
            expected_hash[0] == '\0' ||
            strcmp(response_hash, expected_hash) != 0) {
        PJson_Free(root);
        pdb_set_error(db, "sync schema hash mismatch");
        return PDB_SCHEMA_MISMATCH;
    }
    schema_version_node = PJson_GetObject(root, "schema_version");
    expected_schema_version = pdb_meta_get_int(db, "schema_version", -1);
    if (schema_version_node == NULL ||
            PJson_GetType(schema_version_node) != PJSON_TYPE_NUMBER) {
        PJson_Free(root);
        pdb_set_error(db, "sync response has no schema version");
        return PDB_SCHEMA_MISMATCH;
    }
    if (!PJson_GetDouble(root, "schema_version", &response_schema_value) ||
            response_schema_value < 0.0 ||
            response_schema_value > 2147483647.0 ||
            response_schema_value != (double)(int)response_schema_value) {
        PJson_Free(root);
        pdb_set_error(db, "sync response has an invalid schema version");
        return PDB_SCHEMA_MISMATCH;
    }
    response_schema_version = (int)response_schema_value;
    if (expected_schema_version < 0 ||
            response_schema_version != expected_schema_version) {
        PJson_Free(root);
        pdb_set_error(db, "sync schema version mismatch");
        return PDB_SCHEMA_MISMATCH;
    }
    next_cursor = PJson_GetString(root, "next_cursor");
    if (next_cursor == NULL ||
            !pdb_parse_version(next_cursor, &next_cursor_value) ||
            (int)strlen(next_cursor) >= PDB_SYNC_NAME_MAX) {
        PJson_Free(root);
        pdb_set_error(db, "sync response has no valid next_cursor");
        return PDB_ERROR;
    }
    rc = pdb_meta_get(db, "cursor", stored_cursor, sizeof(stored_cursor));
    if (rc == PDB_NOT_FOUND) {
        pdb_copy_text(stored_cursor, sizeof(stored_cursor), "0");
    } else if (rc != PDB_OK ||
            !pdb_parse_version(stored_cursor, &stored_cursor_value)) {
        PJson_Free(root);
        pdb_set_error(db, "sync database cursor is invalid");
        return PDB_STATE;
    }
    if (rc == PDB_NOT_FOUND) {
        stored_cursor_value = 0;
    }
    if (next_cursor_value < stored_cursor_value) {
        PJson_Free(root);
        pdb_set_error(db, "sync response cursor moved backwards");
        return PDB_ERROR;
    }
    accepted = PJson_GetObject(root, "accepted");
    conflicts = PJson_GetObject(root, "conflicts");
    changes = PJson_GetObject(root, "changes");
    if (accepted == NULL || PJson_GetType(accepted) != PJSON_TYPE_ARRAY ||
            conflicts == NULL || PJson_GetType(conflicts) !=
            PJSON_TYPE_ARRAY || changes == NULL || PJson_GetType(changes) !=
            PJSON_TYPE_ARRAY) {
        PJson_Free(root);
        pdb_set_error(db, "sync response arrays are invalid");
        return PDB_ERROR;
    }
    rc = PDb_Begin(db);
    if (rc == PDB_OK) {
        rc = pdb_process_response_array(db, accepted, 1);
    }
    if (rc == PDB_OK) {
        rc = pdb_process_response_array(db, conflicts, 2);
    }
    if (rc == PDB_OK) {
        rc = pdb_process_response_array(db, changes, 3);
    }
    if (rc == PDB_OK) {
        rc = pdb_meta_set(db, "cursor", next_cursor);
    }
    if (rc == PDB_OK) {
        rc = PDb_Commit(db);
    } else if (db->in_transaction) {
        PDb_Rollback(db);
    }
    PJson_Free(root);
    return rc;
}

static int pdb_count_query(PDbHandle db, const char* sql)
{
    sqlite3_stmt* stmt;
    int count;
    int rc;

    rc = pdb_prepare_internal(db, sql, &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    rc = pdb_step_internal(db, stmt);
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    }
    sqlite3_finalize(stmt);
    return pdb_set_sqlite_error(db, rc);
}

PDB_API int PDb_SyncPendingCount(PDbHandle db)
{
    if (db == NULL || db->mode != PDB_OPEN_SYNC) {
        return PDB_INVALID_ARGUMENT;
    }
    return pdb_count_query(db, "SELECT count(*) FROM __pdb_outbox");
}

PDB_API int PDb_SyncConflictCount(PDbHandle db)
{
    if (db == NULL || db->mode != PDB_OPEN_SYNC) {
        return PDB_INVALID_ARGUMENT;
    }
    return pdb_count_query(db, "SELECT count(*) FROM __pdb_conflict");
}

PDB_API int PDb_SyncGetConflicts(PDbHandle db, char* buffer, int capacity,
        int* outLength)
{
    sqlite3_stmt* stmt;
    PDbWriter writer;
    char* output;
    const unsigned char* text;
    int index;
    int rc;
    int length;

    if (db == NULL || db->mode != PDB_OPEN_SYNC || outLength == NULL ||
            capacity < 0 || (buffer == NULL && capacity != 0)) {
        return PDB_INVALID_ARGUMENT;
    }
    output = (char*)malloc(PDB_MAX_ROW_BYTES);
    if (output == NULL) {
        return PDB_NOMEM;
    }
    pdb_writer_init(&writer, output, PDB_MAX_ROW_BYTES);
    pdb_writer_char(&writer, '[');
    rc = pdb_prepare_internal(db,
            "SELECT id,table_name,row_key,server_version,"
            "local_row_json,server_row_json FROM __pdb_conflict "
            "ORDER BY id LIMIT 257", &stmt);
    if (rc != PDB_OK) {
        free(output);
        return rc;
    }
    index = 0;
    while ((rc = pdb_step_internal(db, stmt)) == SQLITE_ROW) {
        if (index >= 256) {
            sqlite3_finalize(stmt);
            free(output);
            pdb_set_error(db, "conflict list exceeds the response budget");
            return PDB_LIMIT;
        }
        if (index != 0) {
            pdb_writer_char(&writer, ',');
        }
        pdb_writer_literal(&writer, "{\"id\":");
        pdb_writer_int64(&writer, sqlite3_column_int(stmt, 0));
        pdb_writer_literal(&writer, ",\"entity\":");
        text = sqlite3_column_text(stmt, 1);
        pdb_writer_json_string(&writer, (const char*)text);
        pdb_writer_literal(&writer, ",\"key\":");
        text = sqlite3_column_text(stmt, 2);
        pdb_writer_json_string(&writer, (const char*)text);
        pdb_writer_literal(&writer, ",\"server_version\":");
        text = sqlite3_column_text(stmt, 3);
        pdb_writer_json_string(&writer, (const char*)text);
        pdb_writer_literal(&writer, ",\"local_values\":");
        text = sqlite3_column_text(stmt, 4);
        if (text == NULL) {
            pdb_writer_literal(&writer, "null");
        } else {
            pdb_writer_put(&writer, (const char*)text,
                    sqlite3_column_bytes(stmt, 4));
        }
        pdb_writer_literal(&writer, ",\"server_values\":");
        text = sqlite3_column_text(stmt, 5);
        if (text == NULL) {
            pdb_writer_literal(&writer, "null");
        } else {
            pdb_writer_put(&writer, (const char*)text,
                    sqlite3_column_bytes(stmt, 5));
        }
        pdb_writer_char(&writer, '}');
        if (writer.overflow) {
            sqlite3_finalize(stmt);
            free(output);
            return PDB_LIMIT;
        }
        index += 1;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        free(output);
        return pdb_set_sqlite_error(db, rc);
    }
    pdb_writer_char(&writer, ']');
    if (writer.overflow) {
        free(output);
        return PDB_LIMIT;
    }
    length = writer.length;
    *outLength = length;
    if (buffer == NULL || capacity < length + 1) {
        free(output);
        return buffer == NULL && capacity == 0 ? PDB_OK :
                PDB_BUFFER_TOO_SMALL;
    }
    memcpy(buffer, output, (size_t)length + 1);
    free(output);
    return PDB_OK;
}

PDB_API int PDb_SyncCopyConflict(PDbHandle db, int index,
        char* buffer, int capacity, int* outLength)
{
    sqlite3_stmt* stmt;
    PDbWriter writer;
    char* output;
    const unsigned char* text;
    int rc;
    int id;
    int length;

    if (db == NULL || db->mode != PDB_OPEN_SYNC || index < 0 ||
            outLength == NULL || capacity < 0 ||
            (buffer == NULL && capacity != 0)) {
        return PDB_INVALID_ARGUMENT;
    }
    output = (char*)malloc(PDB_MAX_ROW_BYTES);
    if (output == NULL) {
        return PDB_NOMEM;
    }
    rc = pdb_prepare_internal(db,
            "SELECT id,table_name,row_key,server_version,"
            "local_row_json,server_row_json FROM __pdb_conflict "
            "ORDER BY id LIMIT 1 OFFSET ?1", &stmt);
    if (rc != PDB_OK) {
        free(output);
        return rc;
    }
    sqlite3_bind_int(stmt, 1, index);
    rc = pdb_step_internal(db, stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        free(output);
        return PDB_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        free(output);
        return pdb_set_sqlite_error(db, rc);
    }
    pdb_writer_init(&writer, output, PDB_MAX_ROW_BYTES);
    id = sqlite3_column_int(stmt, 0);
    pdb_writer_literal(&writer, "{\"id\":");
    pdb_writer_int64(&writer, id);
    pdb_writer_literal(&writer, ",\"entity\":");
    text = sqlite3_column_text(stmt, 1);
    pdb_writer_json_string(&writer, (const char*)text);
    pdb_writer_literal(&writer, ",\"key\":");
    text = sqlite3_column_text(stmt, 2);
    pdb_writer_json_string(&writer, (const char*)text);
    pdb_writer_literal(&writer, ",\"server_version\":");
    text = sqlite3_column_text(stmt, 3);
    pdb_writer_json_string(&writer, (const char*)text);
    pdb_writer_literal(&writer, ",\"local_values\":");
    text = sqlite3_column_text(stmt, 4);
    if (text == NULL) {
        pdb_writer_literal(&writer, "null");
    } else {
        pdb_writer_put(&writer, (const char*)text,
                sqlite3_column_bytes(stmt, 4));
    }
    pdb_writer_literal(&writer, ",\"server_values\":");
    text = sqlite3_column_text(stmt, 5);
    if (text == NULL) {
        pdb_writer_literal(&writer, "null");
    } else {
        pdb_writer_put(&writer, (const char*)text,
                sqlite3_column_bytes(stmt, 5));
    }
    pdb_writer_char(&writer, '}');
    sqlite3_finalize(stmt);
    if (writer.overflow) {
        free(output);
        return PDB_LIMIT;
    }
    length = writer.length;
    *outLength = length;
    if (buffer == NULL || capacity < length + 1) {
        free(output);
        return buffer == NULL && capacity == 0 ? PDB_OK :
                PDB_BUFFER_TOO_SMALL;
    }
    memcpy(buffer, output, (size_t)length + 1);
    free(output);
    return PDB_OK;
}

static int pdb_load_conflict(PDbHandle db, int conflict_id,
        char* table_name, int table_capacity, char* row_key,
        int key_capacity, char* server_version, int version_capacity,
        char** local_json, char** server_json)
{
    sqlite3_stmt* stmt;
    const unsigned char* text;
    int bytes;
    int rc;

    if (table_name == NULL || row_key == NULL || server_version == NULL ||
            local_json == NULL || server_json == NULL) {
        return PDB_INVALID_ARGUMENT;
    }
    *local_json = NULL;
    *server_json = NULL;
    rc = pdb_prepare_internal(db,
            "SELECT table_name,row_key,server_version,"
            "local_row_json,server_row_json FROM __pdb_conflict "
            "WHERE id=?1", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_int(stmt, 1, conflict_id);
    rc = pdb_step_internal(db, stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return PDB_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return pdb_set_sqlite_error(db, rc);
    }
    text = sqlite3_column_text(stmt, 0);
    pdb_copy_text(table_name, table_capacity, (const char*)text);
    text = sqlite3_column_text(stmt, 1);
    pdb_copy_text(row_key, key_capacity, (const char*)text);
    text = sqlite3_column_text(stmt, 2);
    pdb_copy_text(server_version, version_capacity, (const char*)text);
    text = sqlite3_column_text(stmt, 3);
    if (text != NULL) {
        bytes = sqlite3_column_bytes(stmt, 3);
        *local_json = (char*)malloc((size_t)bytes + 1);
        if (*local_json == NULL) {
            sqlite3_finalize(stmt);
            return PDB_NOMEM;
        }
        memcpy(*local_json, text, (size_t)bytes);
        (*local_json)[bytes] = '\0';
    }
    text = sqlite3_column_text(stmt, 4);
    if (text == NULL) {
        sqlite3_finalize(stmt);
        if (*local_json != NULL) {
            free(*local_json);
            *local_json = NULL;
        }
        return PDB_ERROR;
    }
    bytes = sqlite3_column_bytes(stmt, 4);
    *server_json = (char*)malloc((size_t)bytes + 1);
    if (*server_json == NULL) {
        sqlite3_finalize(stmt);
        if (*local_json != NULL) {
            free(*local_json);
            *local_json = NULL;
        }
        return PDB_NOMEM;
    }
    memcpy(*server_json, text, (size_t)bytes);
    (*server_json)[bytes] = '\0';
    sqlite3_finalize(stmt);
    return PDB_OK;
}

static int pdb_delete_conflict(PDbHandle db, int conflict_id)
{
    sqlite3_stmt* stmt;
    int rc;

    rc = pdb_prepare_internal(db,
            "DELETE FROM __pdb_conflict WHERE id=?1", &stmt);
    if (rc != PDB_OK) {
        return rc;
    }
    sqlite3_bind_int(stmt, 1, conflict_id);
    rc = pdb_step_internal(db, stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? PDB_OK : pdb_set_sqlite_error(db, rc);
}

PDB_API int PDb_SyncResolveConflict(PDbHandle db, int conflict_id,
        int action)
{
    char table_name[PDB_SYNC_NAME_MAX];
    char row_key[PDB_SYNC_NAME_MAX];
    char server_version[PDB_SYNC_NAME_MAX];
    char* local_json;
    char* server_json;
    PDbTableInfo* table;
    int deleted;
    int rc;

    if (db == NULL || db->mode != PDB_OPEN_SYNC || conflict_id <= 0 ||
            (action != PDB_CONFLICT_ACCEPT_SERVER &&
             action != PDB_CONFLICT_RETRY_LOCAL &&
             action != PDB_CONFLICT_DISCARD)) {
        return PDB_INVALID_ARGUMENT;
    }
    local_json = NULL;
    server_json = NULL;
    rc = pdb_load_conflict(db, conflict_id, table_name, sizeof(table_name),
            row_key, sizeof(row_key), server_version,
            sizeof(server_version), &local_json, &server_json);
    if (rc != PDB_OK) {
        return rc;
    }
    table = pdb_find_table(db, table_name) >= 0 ?
            &db->tables[pdb_find_table(db, table_name)] : NULL;
    if (table == NULL) {
        rc = PDB_NOT_FOUND;
        goto resolve_cleanup;
    }
    rc = PDb_Begin(db);
    if (rc != PDB_OK) {
        goto resolve_cleanup;
    }
    if (action == PDB_CONFLICT_RETRY_LOCAL) {
        deleted = local_json == NULL || strcmp(local_json, "null") == 0;
        rc = pdb_apply_row_text(db, table, row_key, local_json, deleted);
        if (rc == PDB_OK) {
            rc = pdb_set_row_state(db, table_name, row_key,
                    server_version, deleted);
        }
        if (rc == PDB_OK) {
            rc = pdb_append_outbox(db, table_name, row_key,
                    deleted ? PDB_DIRTY_DELETE : PDB_DIRTY_UPSERT,
                    deleted ? NULL : local_json, server_version);
        }
    } else {
        deleted = server_json == NULL || strcmp(server_json, "null") == 0;
        rc = pdb_apply_row_text(db, table, row_key, server_json, deleted);
        if (rc == PDB_OK) {
            rc = pdb_set_row_state(db, table_name, row_key,
                    server_version, deleted);
        }
        if (rc == PDB_OK) {
            rc = pdb_delete_outbox_row(db, table_name, row_key);
        }
    }
    if (rc == PDB_OK) {
        rc = pdb_delete_conflict(db, conflict_id);
    }
    if (rc == PDB_OK) {
        rc = PDb_Commit(db);
    } else if (db->in_transaction) {
        PDb_Rollback(db);
    }

resolve_cleanup:
    if (local_json != NULL) {
        free(local_json);
    }
    if (server_json != NULL) {
        free(server_json);
    }
    return rc;
}

BOOL WINAPI DllMain(HANDLE hModule, DWORD reason, LPVOID reserved)
{
    (void)hModule;
    (void)reason;
    (void)reserved;
    return TRUE;
}
