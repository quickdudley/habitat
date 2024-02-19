#ifndef MIGRATE_DB_H
#define MIGRATE_DB_H

#include <Directory.h>
#include <SupportDefs.h>
#include <sqlite3.h>

sqlite3 *migrateToSqlite(const BDirectory &settings);
status_t prepareDatabase(sqlite3 *database);

#endif // MIGRATE_DB_H
