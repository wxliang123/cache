//
//  basic_db.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/17/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#include "db/db_factory.h"

#include <string>

#include "db/a_leveldb.h"
#include "db/a_rocksdb.h"
#include "db/basic_db.h"

using namespace std;
using ycsbc::DB;
using ycsbc::DBFactory;

DB* DBFactory::CreateDB(utils::Properties& props) {
  if (props["dbname"] == "basic") {
    return new BasicDB();
  } else if (props["dbname"] == "leveldb") {
    return new LevelDB();
  } else if (props["dbname"] == "rocksdb") {
    return new RocksDB();
  }
  return nullptr;
}
