#include "MigrateDB.h"
#include <Entry.h>
#include <File.h>
#include <Path.h>
#include <Query.h>
#include <String.h>
#include <Volume.h>
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
                   "blob BLOB"
                   ")",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  if (sqlite3_exec(
          database,
          "CREATE INDEX IF NOT EXISTS typectx ON messages (type, context)",
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
  if (sqlite3_exec(database,
                   "CREATE INDEX IF NOT EXISTS msgtime ON messages (timestamp)",
                   NULL, NULL, &error) != SQLITE_OK) {
    std::cerr << error << std::endl;
    return B_ERROR;
  }
  return B_OK;
}

static void freeBuffer(void *arg) { delete (char *)arg; }

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
      "(cypherkey, author, sequence, timestamp, type, context, blob) "
      "VALUES(?, ?, ?, ?, ?, ?, ?)",
      200, &insert, NULL);
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
    if (BDirectory parent;
        entry.GetParent(&parent) == B_OK && parent == postsDir) {
      entry.Remove();
    }
  }
  sqlite3_finalize(insert);
  return B_OK;
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
  migrateMessages(database, settings);
  return database;
}
