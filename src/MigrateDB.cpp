#include "MigrateDB.h"
#include <Entry.h>
#include <File.h>
#include <Path.h>
#include <Query.h>
#include <String.h>
#include <Volume.h>
#include <cstdlib>
#include <cstring>
#include <iostream>

status_t prepareDatabase(sqlite3 *database) {
  char *error = NULL;
  if (sqlite3_exec(database,
                   "CREATE TABLE IF NOT EXISTS messages("
                   "cypherkey TEXT NOT NULL UNIQUE ON CONFLICT IGNORE, "
                   "author TEXT NOT NULL, "
                   "sequence INTEGER NOT NULL, "
                   "timestamp INTEGER NOT NULL, "
                   "type TEXT, "
                   "context TEXT, "
                   "body BLOB"
                   ")",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  sqlite3_exec(database,
               "ALTER TABLE messages "
               "ADD COLUMN processed INTEGER NOT NULL DEFAULT 0",
               NULL, NULL, &error);
  sqlite3_exec(database,
               "ALTER_TABLE messages "
               "ADD COLUMN viewed INTEGER NOT NULL DEFAULT 0",
               NULL, NULL, &error);
  if (sqlite3_exec(
          database,
          "CREATE INDEX IF NOT EXISTS ctxtype ON messages (context, type)",
          NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  sqlite3_exec(database, "DROP INDEX typectx", NULL, NULL, NULL);
  if (sqlite3_exec(database,
                   "CREATE INDEX IF NOT EXISTS proqueue ON messages (type) "
                   "WHERE processed = 0",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(
          database,
          "CREATE INDEX IF NOT EXISTS authorseq ON messages (author, sequence)",
          NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(
          database,
          "CREATE INDEX IF NOT EXISTS authortype ON messages (author, type)",
          NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(
          database,
          "CREATE INDEX IF NOT EXISTS typetime ON messages (type, timestamp)",
          NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(database,
                   "CREATE INDEX IF NOT EXISTS msgtime ON messages (timestamp)",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(database,
                   "CREATE TABLE IF NOT EXISTS unprocessed(body BLOB)", NULL,
                   NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(database,
                   "CREATE TABLE IF NOT EXISTS feeds "
                   "AS SELECT DISTINCT author FROM messages",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(database,
                   "CREATE UNIQUE INDEX IF NOT EXISTS "
                   "feedkey ON feeds(author)",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(database,
                   "CREATE TABLE IF NOT EXISTS contacts("
                   "author TEXT NOT NULL, "
                   "contact TEXT NOT NULL, "
                   "property TEXT NOT NULL, "
                   "sequence INTEGER NOT NULL, "
                   "value INTEGER NOT NULL)",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(database,
                   "CREATE UNIQUE INDEX IF NOT EXISTS "
                   "colink ON contacts(author, contact, property)",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(database,
                   "CREATE TABLE IF NOT EXISTS profiles("
                   "author TEXT NOT NULL, "
                   "property TEXT NOT NULL, "
                   "sequence INTEGER NOT NULL, "
                   "type INTEGER NOT NULL, "
                   "value TEXT)",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(database,
                   "CREATE UNIQUE INDEX IF NOT EXISTS "
                   "uprop ON profiles(author, property)",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  return B_OK;
}

static void freeBuffer(void *arg) { delete[] (char *)arg; }

static inline status_t migrateMessages(sqlite3 *database,
                                       const BDirectory &settings) {
  BDirectory postsDir;
  {
    BEntry postsEntry;
    settings.FindEntry("posts", &postsEntry);
    postsDir.SetTo(&postsEntry);
  }
  BQuery query;
  {
    BVolume volume;
    settings.GetVolume(&volume);
    query.SetVolume(&volume);
  }
  query.SetPredicate("HABITAT:author=\"**\"");
  query.Fetch();
  BEntry entry;
  sqlite3_stmt *insert;
  sqlite3_prepare_v2(
      database,
      "INSERT INTO messages"
      "(cypherkey, author, sequence, timestamp, type, context, body) "
      "VALUES(?, ?, ?, ?, ?, ?, ?)",
      200, &insert, NULL);
  sqlite3_stmt *verify;
  sqlite3_prepare_v2(
      database,
      "SELECT cypherkey, author, sequence, timestamp, type, context, body "
      "FROM messages WHERE cypherkey = ?",
      -1, &verify, NULL);
  while (query.GetNextEntry(&entry) == B_OK) {
    BMessage blob;
    BString cypherkey;
    BString author;
    int64 sequence;
    int64 timestamp;
    BString type;
    BString context;
    bool hasType;
    bool hasContext;
    {
      BFile file(&entry, B_READ_ONLY);
      blob.Unflatten(&file);
      file.ReadAttrString("HABITAT:cypherkey", &cypherkey);
      file.ReadAttrString("HABITAT:author", &author);
      file.ReadAttr("HABITAT:sequence", B_INT64_TYPE, 0, &sequence,
                    sizeof(int64));
      file.ReadAttr("HABITAT:timestamp", B_INT64_TYPE, 0, &timestamp,
                    sizeof(int64));
      hasType = file.ReadAttrString("HABITAT:type", &type) == B_OK;
      hasContext = file.ReadAttrString("HABITAT:context", &context) == B_OK;
    }
    sqlite3_bind_text(insert, 1, cypherkey.String(), cypherkey.Length(),
                      SQLITE_STATIC);
    sqlite3_bind_text(insert, 2, author.String(), author.Length(),
                      SQLITE_STATIC);
    sqlite3_bind_int64(insert, 3, sequence);
    sqlite3_bind_int64(insert, 4, timestamp);
    if (hasType)
      sqlite3_bind_text(insert, 5, type.String(), type.Length(), SQLITE_STATIC);
    else
      sqlite3_bind_null(insert, 5);
    if (hasContext) {
      sqlite3_bind_text(insert, 6, context.String(), context.Length(),
                        SQLITE_STATIC);
    } else {
      sqlite3_bind_null(insert, 6);
    }
    ssize_t flatSize = blob.FlattenedSize();
    char *buffer = new char[flatSize];
    blob.Flatten(buffer, flatSize);
    sqlite3_bind_blob64(insert, 7, buffer, flatSize, freeBuffer);
    sqlite3_step(insert);
    sqlite3_reset(insert);
    // Verify that the new row equals what we expect it to.
    sqlite3_bind_text(verify, 1, cypherkey.String(), cypherkey.Length(),
                      SQLITE_STATIC);
    if (sqlite3_step(verify) != SQLITE_ROW) {
      std::cerr << "Could not find the row just inserted." << std::endl;
      exit(-1);
    }
    if (cypherkey != (const char *)sqlite3_column_text(verify, 0)) {
      std::cerr << "Inserted cypherkey mismatch!" << std::endl;
      std::cerr << cypherkey.String()
                << " != " << sqlite3_column_text(verify, 1) << std::endl;
      exit(-1);
    }
    if (author != (const char *)sqlite3_column_text(verify, 1)) {
      std::cerr << "Inserted author mismatch!" << std::endl;
      exit(-1);
    }
    if (sequence != sqlite3_column_int64(verify, 2)) {
      std::cerr << "Inserted sequence mismatch!" << std::endl;
      exit(-1);
    }
    if (timestamp != sqlite3_column_int64(verify, 3)) {
      std::cerr << "Inserted timestamp mismatch!" << std::endl;
      exit(-1);
    }
    if (type != (const char *)sqlite3_column_text(verify, 4)) {
      std::cerr << "Inserted type mismatch!" << std::endl;
      exit(-1);
    }
    if (context != "" &&
        context != (const char *)sqlite3_column_text(verify, 5)) {
      std::cerr << "Inserted context mismatch" << std::endl;
      exit(-1);
    }
    if (sqlite3_column_bytes(verify, 6) != flatSize ||
        std::memcmp(sqlite3_column_blob(verify, 6), buffer, flatSize) != 0) {
      std::cerr << "Inserted blob mismatch" << std::endl;
      exit(-1);
    }
    sqlite3_reset(verify);
    if (BDirectory parent;
        entry.GetParent(&parent) == B_OK && parent == postsDir) {
      entry.Remove();
    }
  }
  sqlite3_finalize(verify);
  sqlite3_finalize(insert);
  return B_OK;
}

static inline void setWal(sqlite3 *database) {
  char *error = NULL;
  sqlite3_exec(database, "PRAGMA journal_mode = WAL", NULL, NULL, &error);
  sqlite3_exec(database, "pragma wal_checkpoint(truncate)", NULL, NULL, &error);
}

sqlite3 *migrateToSqlite(const BDirectory &settings) {
  sqlite3 *database;
  {
    BEntry dbEntry(&settings, "database.sqlite3");
    BPath dbPath;
    dbEntry.GetPath(&dbPath);
    sqlite3_open(dbPath.Path(), &database);
  }
  if (prepareDatabase(database) != B_OK) {
    sqlite3_close(database);
    return NULL;
  }
  setWal(database);
  migrateMessages(database, settings);
  return database;
}
