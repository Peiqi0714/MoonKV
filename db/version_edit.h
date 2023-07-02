//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once
#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "db/blob/blob_file_addition.h"
#include "db/blob/blob_file_garbage.h"
#include "db/dbformat.h"
#include "db/wal_edit.h"
#include "memory/arena.h"
#include "port/malloc.h"
#include "rocksdb/advanced_options.h"
#include "rocksdb/cache.h"
#include "table/table_reader.h"
#include "util/autovector.h"
#include "nvm/index/position_key_list.h"

namespace ROCKSDB_NAMESPACE {

// Tag numbers for serialized VersionEdit.  These numbers are written to
// disk and should not be changed. The number should be forward compatible so
// users can down-grade RocksDB safely. A future Tag is ignored by doing '&'
// between Tag and kTagSafeIgnoreMask field.
enum Tag : uint32_t {
  kComparator = 1,
  kLogNumber = 2,
  kNextFileNumber = 3,
  kLastSequence = 4,
  kCompactCursor = 5,
  kDeletedFile = 6,
  kNewFile = 7,
  // 8 was used for large value refs
  kPrevLogNumber = 9,
  kMinLogNumberToKeep = 10,

  // these are new formats divergent from open source leveldb
  kNewFile2 = 100,
  kNewFile3 = 102,
  kNewFile4 = 103,      // 4th (the latest) format version of adding files
  kColumnFamily = 200,  // specify column family for version edit
  kColumnFamilyAdd = 201,
  kColumnFamilyDrop = 202,
  kMaxColumnFamily = 203,

  kInAtomicGroup = 300,

  kBlobFileAddition = 400,
  kBlobFileGarbage,

  // Mask for an unidentified tag from the future which can be safely ignored.
  kTagSafeIgnoreMask = 1 << 13,

  // Forward compatible (aka ignorable) records
  kDbId,
  kBlobFileAddition_DEPRECATED,
  kBlobFileGarbage_DEPRECATED,
  kWalAddition,
  kWalDeletion,
  kFullHistoryTsLow,
  kWalAddition2,
  kWalDeletion2,
};

enum NewFileCustomTag : uint32_t {
  kTerminate = 1,  // The end of customized fields
  kNeedCompaction = 2,
  // Since Manifest is not entirely forward-compatible, we currently encode
  // kMinLogNumberToKeep as part of NewFile as a hack. This should be removed
  // when manifest becomes forward-compatible.
  kMinLogNumberToKeepHack = 3,
  kOldestBlobFileNumber = 4,
  kOldestAncesterTime = 5,
  kFileCreationTime = 6,
  kFileChecksum = 7,
  kFileChecksumFuncName = 8,
  kTemperature = 9,
  kMinTimestamp = 10,
  kMaxTimestamp = 11,
  kUniqueId = 12,

  // If this bit for the custom tag is set, opening DB should fail if
  // we don't know this field.
  kCustomTagNonSafeIgnoreMask = 1 << 6,

  // Forward incompatible (aka unignorable) fields
  kPathId,
};

class VersionSet;


constexpr uint64_t kFileNumberMask = 0x0FFFFFFFFFFFFFFF;
constexpr uint64_t kUnknownOldestAncesterTime = 0;
constexpr uint64_t kUnknownFileCreationTime = 0;

extern uint64_t PackFileNumberAndPathId(uint64_t number, uint64_t path_id);

using UniqueId64x2 = std::array<uint64_t, 2>;

// A copyable structure contains information needed to read data from an SST
// file. It can contain a pointer to a table reader opened for the file, or
// file number and size, which can be used to create a new table reader for it.
// The behavior is undefined when a copied of the structure is used when the
// file is not in any live version any more.
struct FileDescriptor {
  // Table reader in table_reader_handle
  TableReader* table_reader;
  uint64_t packed_number_and_path_id;
  std::map<uint32_t,uint32_t> sub_number_to_reference_key; // 仅在NvmPartition中有效 存储NvmPartition中指向的所有NvmTable的table file number的对应的指向有效key的映射
  std::map<uint32_t,uint32_t> father_number_to_reference_key; // 仅在NvmTable中有效 存储所有指向NvmTable的index file number和其指向的key的个数的计数
  //uint32_t index_refs; // 仅在NvmTable中有效 存储指向NvmTable的所有的NvmBtreee的引用数
  uint64_t file_size;  // File size in bytes
  uint64_t sub_file_size; // 仅在NvmBtree中有效 表示指向的Nvmtable数据大小
  SequenceNumber smallest_seqno;  // The smallest seqno in this file
  SequenceNumber largest_seqno;   // The largest seqno in this file

  FileDescriptor() : FileDescriptor(0, 0, 0) {}

  FileDescriptor(uint64_t number, uint32_t path_id, uint64_t _file_size)
      : FileDescriptor(number, path_id, std::map<uint32_t,uint32_t>(),std::map<uint32_t,uint32_t>(), _file_size, 0, kMaxSequenceNumber, 0) {}

  FileDescriptor(uint64_t number, uint32_t path_id, uint64_t _file_size,
                 SequenceNumber _smallest_seqno, SequenceNumber _largest_seqno)
      : FileDescriptor(number, path_id, std::map<uint32_t,uint32_t>(),std::map<uint32_t,uint32_t>(), _file_size, 0, _smallest_seqno, _largest_seqno) {}

  FileDescriptor(uint64_t number, uint32_t path_id, uint64_t _file_size,uint64_t _sub_file_size)
      : FileDescriptor(number, path_id, std::map<uint32_t,uint32_t>(), std::map<uint32_t,uint32_t>(), _file_size, _sub_file_size,kMaxSequenceNumber, 0) {}

  FileDescriptor(uint64_t number, uint32_t path_id, uint64_t _file_size,uint64_t _sub_file_size,
                 SequenceNumber _smallest_seqno, SequenceNumber _largest_seqno)
      : FileDescriptor(number, path_id, std::map<uint32_t,uint32_t>(),std::map<uint32_t,uint32_t>(),_file_size,_sub_file_size, _smallest_seqno, _largest_seqno) {}

  FileDescriptor(uint64_t number, uint32_t path_id,std::map<uint32_t,uint32_t> _sub_number_to_reference_key,std::map<uint32_t,uint32_t> _father_number_to_reference_key,/*uint32_t _index_ref,*/uint64_t _file_size,uint64_t _sub_file_size,
                 SequenceNumber _smallest_seqno, SequenceNumber _largest_seqno)
      :table_reader(nullptr),
       packed_number_and_path_id(PackFileNumberAndPathId(number, path_id)),
       sub_number_to_reference_key(_sub_number_to_reference_key),
       father_number_to_reference_key(_father_number_to_reference_key),
      //  index_refs(_index_ref),
       file_size(_file_size),
       sub_file_size(_sub_file_size),
       smallest_seqno(_smallest_seqno),
       largest_seqno(_largest_seqno){}

  FileDescriptor(const FileDescriptor& fd) { *this = fd; }

  FileDescriptor& operator=(const FileDescriptor& fd) {
    table_reader = fd.table_reader;
    packed_number_and_path_id = fd.packed_number_and_path_id;
    sub_number_to_reference_key = fd.sub_number_to_reference_key;
    father_number_to_reference_key = fd.father_number_to_reference_key;
    // index_refs = fd.index_refs;
    file_size = fd.file_size;
    sub_file_size = fd.sub_file_size;
    smallest_seqno = fd.smallest_seqno;
    largest_seqno = fd.largest_seqno;
    return *this;
  }

  uint64_t GetNumber() const {
    return packed_number_and_path_id & kFileNumberMask;
  }
  uint32_t GetPathId() const {
    return static_cast<uint32_t>(
        packed_number_and_path_id / (kFileNumberMask + 1));
  }
  uint64_t GetFileSize() const { return file_size; }
  uint64_t GetSubFileSize() const { return sub_file_size; }
  const std::map<uint32_t,uint32_t>& GetSubNumberToReferencekey() const {
    return sub_number_to_reference_key;
  }
  // uint32_t GetIndexRefs() const {
  //   return index_refs;
  // }
  // void SetIndexRefs(uint32_t _index_refs) {
  //   index_refs = _index_refs;
  // }
  const std::map<uint32_t,uint32_t>& GetFatherNumberToReferencekey() const {
    return father_number_to_reference_key;
  }
};

struct FileSampledStats {
  FileSampledStats() : num_reads_sampled(0) {}
  FileSampledStats(const FileSampledStats& other) { *this = other; }
  FileSampledStats& operator=(const FileSampledStats& other) {
    num_reads_sampled = other.num_reads_sampled.load();
    return *this;
  }

  // number of user reads to this file.
  mutable std::atomic<uint64_t> num_reads_sampled;
};

struct FileMetaData {
  FileDescriptor fd;
  InternalKey smallest;            // Smallest internal key served by table
  InternalKey largest;             // Largest internal key served by table

  // Needs to be disposed when refs becomes 0.
  Cache::Handle* table_reader_handle = nullptr;

  FileSampledStats stats;

  // Stats for compensating deletion entries during compaction

  // File size compensated by deletion entry.
  // This is updated in Version::UpdateAccumulatedStats() first time when the
  // file is created or loaded.  After it is updated (!= 0), it is immutable.
  uint64_t compensated_file_size = 0;
  // These values can mutate, but they can only be read or written from
  // single-threaded LogAndApply thread
  uint64_t num_entries = 0;     // the number of entries.
  uint64_t num_deletions = 0;   // the number of deletion entries.
  uint64_t raw_key_size = 0;    // total uncompressed key size.
  uint64_t raw_value_size = 0;  // total uncompressed value size.

  int refs = 0;  // Reference count 在NvmBtree中意为指向该文件的version个数 在NvmTable中意为指向该文件的NvmBtree个数

  bool is_deleted = false; // 标识本fmd是否已经被添加过obsolete文件中

  // std::vector<uint32_t> children_ranks_; // 仅在NvmBtree中有用 children_ranks_中存储了所有下一level的guard的边界在当前file中的key偏移数量
  // std::vector<std::pair<uint32_t,uint32_t>> children_ranks_; // 仅在NvmBtree中有用 children_ranks_中存储了所有下一level的file的边界在当前file中的key偏移数量
  std::vector<PositionKeyList>  children_ranks_; // 仅在NvmBtree中有用 children_ranks_中存储了所有当前file和下一level的overlapped file之间的range对应(注意，对于level0文件来说，第一个值一定代表level0内的预估值)

  // 为什么上面有num_entries，这里还要加一个total_entries？因为上面的num_entries的赋值逻辑并不是每次生成一个新的Fmd都会有，而是在特定的地方赋值...
  // 懒得看这里的逻辑了，干脆新增两个字段吧。
  uint64_t total_entries_ = 0; // 在NvmBtree和NvmTable中有用，表示本index/table中的总key数量
  uint64_t reference_entries_ = 0; //仅在NvmTable中有用，表示本table中过期key的数量(这里的过期并不是指lsm-tree维度的更高level的过期，而是经历了index compaction之后的过期)
  uint64_t merge_entries_ = 0; // 仅在NvmBtree中有用 表示本index中的merge类型占比总数量

  bool being_compacted = false;       // Is this file undergoing compaction?
  bool init_stats_from_file = false;  // true if the data-entry stats of this
                                      // file has initialized from file.

  bool marked_for_compaction = false;  // True if client asked us nicely to
                                       // compact this file.
  Temperature temperature = Temperature::kUnknown;

  // Used only in BlobDB. The file number of the oldest blob file this SST file
  // refers to. 0 is an invalid value; BlobDB numbers the files starting from 1.
  uint64_t oldest_blob_file_number = kInvalidBlobFileNumber;

  // The file could be the compaction output from other SST files, which could
  // in turn be outputs for compact older SST files. We track the memtable
  // flush timestamp for the oldest SST file that eventually contribute data
  // to this file. 0 means the information is not available.
  uint64_t oldest_ancester_time = kUnknownOldestAncesterTime;

  // Unix time when the SST file is created.
  uint64_t file_creation_time = kUnknownFileCreationTime;

  // File checksum
  std::string file_checksum = kUnknownFileChecksum;

  // File checksum function name
  std::string file_checksum_func_name = kUnknownFileChecksumFuncName;
  // Min (oldest) timestamp of keys in this file
  std::string min_timestamp;
  // Max (newest) timestamp of keys in this file
  std::string max_timestamp;

  // SST unique id
  UniqueId64x2 unique_id{};

  FileMetaData() = default;

  FileMetaData(uint64_t file, uint32_t file_path_id, uint64_t file_size,
               const InternalKey& smallest_key, const InternalKey& largest_key,
               const SequenceNumber& smallest_seq,
               const SequenceNumber& largest_seq, bool marked_for_compact,
               Temperature _temperature, uint64_t oldest_blob_file,
               uint64_t _oldest_ancester_time, uint64_t _file_creation_time,
               const std::string& _file_checksum,
               const std::string& _file_checksum_func_name,
               std::string _min_timestamp, std::string _max_timestamp,
               UniqueId64x2 _unique_id)
      : FileMetaData(file, file_path_id,std::vector<PositionKeyList>(), 0, 0,0, std::map<uint32_t,uint32_t>(), std::map<uint32_t,uint32_t>(), file_size,0, smallest_key, largest_key,
                     smallest_seq, largest_seq, marked_for_compact, _temperature,
                     oldest_blob_file, _oldest_ancester_time, _file_creation_time,
                     _file_checksum, _file_checksum_func_name,
                     std::move(_min_timestamp), std::move(_max_timestamp),
                     std::move(_unique_id)) {}

  FileMetaData(uint64_t file, uint32_t file_path_id, std::vector<PositionKeyList> children_ranks, uint64_t total_entries, uint64_t reference_entries, std::map<uint32_t,uint32_t> sub_number_to_reference_key, std::map<uint32_t,uint32_t> father_number_to_reference_key,/*uint32_t index_refs,*/uint64_t file_size,uint64_t sub_file_size,
               const InternalKey& smallest_key, const InternalKey& largest_key,
               const SequenceNumber& smallest_seq,
               const SequenceNumber& largest_seq, bool marked_for_compact,
               Temperature _temperature, uint64_t oldest_blob_file,
               uint64_t _oldest_ancester_time, uint64_t _file_creation_time,
               const std::string& _file_checksum,
               const std::string& _file_checksum_func_name,
               std::string _min_timestamp, std::string _max_timestamp,
               UniqueId64x2 _unique_id)
      : fd(file, file_path_id, sub_number_to_reference_key,father_number_to_reference_key,/*index_refs,*/file_size,sub_file_size, smallest_seq, largest_seq),
        smallest(smallest_key),
        largest(largest_key),
        children_ranks_(children_ranks),
        total_entries_(total_entries),
        reference_entries_(reference_entries),
        marked_for_compaction(marked_for_compact),
        temperature(_temperature),
        oldest_blob_file_number(oldest_blob_file),
        oldest_ancester_time(_oldest_ancester_time),
        file_creation_time(_file_creation_time),
        file_checksum(_file_checksum),
        file_checksum_func_name(_file_checksum_func_name),
        min_timestamp(std::move(_min_timestamp)),
        max_timestamp(std::move(_max_timestamp)),
        unique_id(std::move(_unique_id)) {
    TEST_SYNC_POINT_CALLBACK("FileMetaData::FileMetaData", this);
  }

  FileMetaData(uint64_t file, uint32_t file_path_id, std::vector<PositionKeyList> children_ranks, uint64_t total_entries, uint64_t reference_entries,uint64_t merge_entries, std::map<uint32_t,uint32_t> sub_number_to_reference_key, std::map<uint32_t,uint32_t> father_number_to_reference_key,/*uint32_t index_refs,*/uint64_t file_size,uint64_t sub_file_size,
               const InternalKey& smallest_key, const InternalKey& largest_key,
               const SequenceNumber& smallest_seq,
               const SequenceNumber& largest_seq, bool marked_for_compact,
               Temperature _temperature, uint64_t oldest_blob_file,
               uint64_t _oldest_ancester_time, uint64_t _file_creation_time,
               const std::string& _file_checksum,
               const std::string& _file_checksum_func_name,
               std::string _min_timestamp, std::string _max_timestamp,
               UniqueId64x2 _unique_id)
      : fd(file, file_path_id, sub_number_to_reference_key,father_number_to_reference_key,/*index_refs,*/file_size,sub_file_size, smallest_seq, largest_seq),
        smallest(smallest_key),
        largest(largest_key),
        children_ranks_(children_ranks),
        total_entries_(total_entries),
        reference_entries_(reference_entries),
        merge_entries_(merge_entries),
        marked_for_compaction(marked_for_compact),
        temperature(_temperature),
        oldest_blob_file_number(oldest_blob_file),
        oldest_ancester_time(_oldest_ancester_time),
        file_creation_time(_file_creation_time),
        file_checksum(_file_checksum),
        file_checksum_func_name(_file_checksum_func_name),
        min_timestamp(std::move(_min_timestamp)),
        max_timestamp(std::move(_max_timestamp)),
        unique_id(std::move(_unique_id)) {
    TEST_SYNC_POINT_CALLBACK("FileMetaData::FileMetaData", this);
  }


  // REQUIRED: Keys must be given to the function in sorted order (it expects
  // the last key to be the largest).
  Status UpdateBoundaries(const Slice& key, const Slice& value,
                          SequenceNumber seqno, ValueType value_type);

  // Unlike UpdateBoundaries, ranges do not need to be presented in any
  // particular order.
  void UpdateBoundariesForRange(const InternalKey& start,
                                const InternalKey& end, SequenceNumber seqno,
                                const InternalKeyComparator& icmp) {
    if (smallest.size() == 0 || icmp.Compare(start, smallest) < 0) {
      smallest = start;
    }
    if (largest.size() == 0 || icmp.Compare(largest, end) < 0) {
      largest = end;
    }
    fd.smallest_seqno = std::min(fd.smallest_seqno, seqno);
    fd.largest_seqno = std::max(fd.largest_seqno, seqno);
  }

  // Try to get oldest ancester time from the class itself or table properties
  // if table reader is already pinned.
  // 0 means the information is not available.
  uint64_t TryGetOldestAncesterTime() {
    if (oldest_ancester_time != kUnknownOldestAncesterTime) {
      return oldest_ancester_time;
    } else if (fd.table_reader != nullptr &&
               fd.table_reader->GetTableProperties() != nullptr) {
      return fd.table_reader->GetTableProperties()->creation_time;
    }
    return kUnknownOldestAncesterTime;
  }

  uint64_t TryGetFileCreationTime() {
    if (file_creation_time != kUnknownFileCreationTime) {
      return file_creation_time;
    } else if (fd.table_reader != nullptr &&
               fd.table_reader->GetTableProperties() != nullptr) {
      return fd.table_reader->GetTableProperties()->file_creation_time;
    }
    return kUnknownFileCreationTime;
  }

  // WARNING: manual update to this function is needed
  // whenever a new string property is added to FileMetaData
  // to reduce approximation error.
  //
  // TODO: eliminate the need of manually updating this function
  // for new string properties
  size_t ApproximateMemoryUsage() const {
    size_t usage = 0;
#ifdef ROCKSDB_MALLOC_USABLE_SIZE
    usage += malloc_usable_size(const_cast<FileMetaData*>(this));
#else
    usage += sizeof(*this);
#endif  // ROCKSDB_MALLOC_USABLE_SIZE
    usage += smallest.size() + largest.size() + file_checksum.size() +
             file_checksum_func_name.size() + min_timestamp.size() +
             max_timestamp.size();
    return usage;
  }
};

// A compressed copy of file meta data that just contain minimum data needed
// to serve read operations, while still keeping the pointer to full metadata
// of the file in case it is needed.
struct FdWithKeyRange {
  FileDescriptor fd;
  FileMetaData* file_metadata;  // Point to all metadata
  Slice smallest_key;    // slice that contain smallest key
  Slice largest_key;     // slice that contain largest key

  FdWithKeyRange()
      : fd(),
        file_metadata(nullptr),
        smallest_key(),
        largest_key() {
  }

  FdWithKeyRange(FileDescriptor _fd, Slice _smallest_key, Slice _largest_key,
                 FileMetaData* _file_metadata)
      : fd(_fd),
        file_metadata(_file_metadata),
        smallest_key(_smallest_key),
        largest_key(_largest_key) {}
};

// Data structure to store an array of FdWithKeyRange in one level
// Actual data is guaranteed to be stored closely
struct LevelFilesBrief {
  size_t num_files;
  FdWithKeyRange* files;
  LevelFilesBrief() {
    num_files = 0;
    files = nullptr;
  }
};

// The state of a DB at any given time is referred to as a Version.
// Any modification to the Version is considered a Version Edit. A Version is
// constructed by joining a sequence of Version Edits. Version Edits are written
// to the MANIFEST file.
class VersionEdit {
 public:
  void Clear();

  void SetDBId(const std::string& db_id) {
    has_db_id_ = true;
    db_id_ = db_id;
  }
  bool HasDbId() const { return has_db_id_; }
  const std::string& GetDbId() const { return db_id_; }

  void SetComparatorName(const Slice& name) {
    has_comparator_ = true;
    comparator_ = name.ToString();
  }
  bool HasComparatorName() const { return has_comparator_; }
  const std::string& GetComparatorName() const { return comparator_; }

  void SetLogNumber(uint64_t num) {
    has_log_number_ = true;
    log_number_ = num;
  }
  bool HasLogNumber() const { return has_log_number_; }
  uint64_t GetLogNumber() const { return log_number_; }

  void SetPrevLogNumber(uint64_t num) {
    has_prev_log_number_ = true;
    prev_log_number_ = num;
  }
  bool HasPrevLogNumber() const { return has_prev_log_number_; }
  uint64_t GetPrevLogNumber() const { return prev_log_number_; }

  void SetNextFile(uint64_t num) {
    has_next_file_number_ = true;
    next_file_number_ = num;
  }
  bool HasNextFile() const { return has_next_file_number_; }
  uint64_t GetNextFile() const { return next_file_number_; }

  void SetMaxColumnFamily(uint32_t max_column_family) {
    has_max_column_family_ = true;
    max_column_family_ = max_column_family;
  }
  bool HasMaxColumnFamily() const { return has_max_column_family_; }
  uint32_t GetMaxColumnFamily() const { return max_column_family_; }

  void SetMinLogNumberToKeep(uint64_t num) {
    has_min_log_number_to_keep_ = true;
    min_log_number_to_keep_ = num;
  }
  bool HasMinLogNumberToKeep() const { return has_min_log_number_to_keep_; }
  uint64_t GetMinLogNumberToKeep() const { return min_log_number_to_keep_; }

  void SetLastSequence(SequenceNumber seq) {
    has_last_sequence_ = true;
    last_sequence_ = seq;
  }
  bool HasLastSequence() const { return has_last_sequence_; }
  SequenceNumber GetLastSequence() const { return last_sequence_; }

  // Delete the specified table file from the specified level.
  void DeleteFile(int level, uint64_t file) {
    deleted_files_.emplace(level, file);
  }

  // Retrieve the table files deleted as well as their associated levels.
  using DeletedFiles = std::set<std::pair<int, uint64_t>>;
  const DeletedFiles& GetDeletedFiles() const { return deleted_files_; }

  // Add the specified table file at the specified level.
  // REQUIRES: "smallest" and "largest" are smallest and largest keys in file
  // REQUIRES: "oldest_blob_file_number" is the number of the oldest blob file
  // referred to by this file if any, kInvalidBlobFileNumber otherwise.
  void AddFile(int level, uint64_t file, uint32_t file_path_id,
               uint64_t file_size, const InternalKey& smallest,
               const InternalKey& largest, const SequenceNumber& smallest_seqno,
               const SequenceNumber& largest_seqno, bool marked_for_compaction,
               Temperature temperature, uint64_t oldest_blob_file_number,
               uint64_t oldest_ancester_time, uint64_t file_creation_time,
               const std::string& file_checksum,
               const std::string& file_checksum_func_name,
               const std::string& min_timestamp,
               const std::string& max_timestamp,
               const UniqueId64x2& unique_id) {
    assert(smallest_seqno <= largest_seqno);
    new_files_.emplace_back(
        level,
        FileMetaData(file, file_path_id, file_size, smallest, largest,
                     smallest_seqno, largest_seqno, marked_for_compaction,
                     temperature, oldest_blob_file_number, oldest_ancester_time,
                     file_creation_time, file_checksum, file_checksum_func_name,
                     min_timestamp, max_timestamp, unique_id));
    if (!HasLastSequence() || largest_seqno > GetLastSequence()) {
      SetLastSequence(largest_seqno);
    }
  }
  void AddFile(int level, uint64_t file, uint32_t file_path_id, std::vector<PositionKeyList> children_ranks, uint64_t total_entries, uint64_t merge_entries,
               std::map<uint32_t,uint32_t> sub_number_to_reference_key,
               uint64_t file_size, const InternalKey& smallest,
               const InternalKey& largest, const SequenceNumber& smallest_seqno,
               const SequenceNumber& largest_seqno, bool marked_for_compaction,
               Temperature temperature, uint64_t oldest_blob_file_number,
               uint64_t oldest_ancester_time, uint64_t file_creation_time,
               const std::string& file_checksum,
               const std::string& file_checksum_func_name,
               const std::string& min_timestamp,
               const std::string& max_timestamp,
               const UniqueId64x2& unique_id) {
    assert(smallest_seqno <= largest_seqno);
    new_files_.emplace_back(
        level,
        FileMetaData(file, file_path_id, children_ranks, total_entries, total_entries/* index file中的reference_entries_无用 */,merge_entries, sub_number_to_reference_key, std::map<uint32_t,uint32_t>()/*在index file中 father_number_to_reference_key无用*/,/*0 暂时无用,*/file_size, 0, smallest, largest,  
                     smallest_seqno, largest_seqno, marked_for_compaction,
                     temperature, oldest_blob_file_number, oldest_ancester_time,
                     file_creation_time, file_checksum, file_checksum_func_name,
                     min_timestamp, max_timestamp, unique_id));
    if (!HasLastSequence() || largest_seqno > GetLastSequence()) {
      SetLastSequence(largest_seqno);
    }
  }

  void AddFile(int level, const FileMetaData& f) {
    assert(f.fd.smallest_seqno <= f.fd.largest_seqno);
    new_files_.emplace_back(level, f);
    if (!HasLastSequence() || f.fd.largest_seqno > GetLastSequence()) {
      SetLastSequence(f.fd.largest_seqno);
    }
  }

  void AddTableFile(const FileMetaData& f){
    assert(f.fd.smallest_seqno <= f.fd.largest_seqno);
    new_table_files_.emplace_back(f);
    if (!HasLastSequence() || f.fd.largest_seqno > GetLastSequence()) {
      SetLastSequence(f.fd.largest_seqno);
    } 
  }

  void AddTableFile(uint64_t file, uint32_t file_path_id, uint32_t total_entries,
               uint64_t file_size, const InternalKey& smallest,
               const InternalKey& largest, const SequenceNumber& smallest_seqno,
               const SequenceNumber& largest_seqno, bool marked_for_compaction,
               Temperature temperature, uint64_t oldest_blob_file_number,
               uint64_t oldest_ancester_time, uint64_t file_creation_time,
               const std::string& file_checksum,
               const std::string& file_checksum_func_name,
               const std::string& min_timestamp,
               const std::string& max_timestamp,
               const UniqueId64x2& unique_id) {
    assert(smallest_seqno <= largest_seqno);
    new_table_files_.emplace_back(
        // 为什么这里添加table_file的reference_entries_为0，father_number_to_reference_key为空呢？我是这么想的。table_file的reference_entries_的变化存在于flush、value compaction和index compaction中。
        // 在这index compaction中，一个table_file的最终father_number_to_reference_key和reference_entries_都可以通过version的所有index file的sub_number_to_reference_key来计算。
        // 上述的计算在SaveTableFilesTo函数中很容易实现
        // 如果在这里添加table file的时候就设置了reference_entries_为0，在value compaction的情况下，会出现添加两次reference_entries_的情况(SaveTableFilesTo函数中会添加一次)
        // 而index_file的total_entries和sub_number_to_reference_key在compaction/flush结束后就可以确定，不需要后续在Save的逻辑中重新计算捏。
        FileMetaData(file, file_path_id,std::vector<PositionKeyList>(), total_entries, 0, 0, std::map<uint32_t,uint32_t>()/* sub_number_to_reference_key在table_file中无用 */, std::map<uint32_t,uint32_t>(), /*1 ,*/ file_size,0, smallest, largest, 
                     smallest_seqno, largest_seqno, marked_for_compaction,
                     temperature, oldest_blob_file_number, oldest_ancester_time,
                     file_creation_time, file_checksum, file_checksum_func_name,
                     min_timestamp, max_timestamp, unique_id));
    if (!HasLastSequence() || largest_seqno > GetLastSequence()) {
      SetLastSequence(largest_seqno);
    }
  }

  // func：简单封装了一下AddFile和AddTableFile 只在Flush和带ValueCompaction的compaction中有用
  // 此函数无需传入sub_numbers参数 因为在这种情况下，构造的index file的sub_number一定只包含一个元素，即data file的file number
  // void AddFileAndTableFile(int level, uint64_t file, uint64_t table_file, std::vector<uint32_t> children_ranks, uint64_t total_entries, uint64_t reference_entries_, uint32_t file_path_id,
  //              uint64_t file_size, uint64_t sub_file_size, const InternalKey& smallest,
  //              const InternalKey& largest, const SequenceNumber& smallest_seqno,
  //              const SequenceNumber& largest_seqno, bool marked_for_compaction,
  //              Temperature temperature, uint64_t oldest_blob_file_number,
  //              uint64_t oldest_ancester_time, uint64_t file_creation_time,
  //              const std::string& file_checksum,
  //              const std::string& file_checksum_func_name,
  //              const std::string& min_timestamp,
  //              const std::string& max_timestamp,
  //              const UniqueId64x2& unique_id) {
  //   AddFile(level, file, file_path_id,children_ranks, total_entries, std::map<uint32_t,uint32_t>{{table_file,reference_entries_}}, file_size, smallest, largest,
  //           smallest_seqno, largest_seqno, marked_for_compaction, temperature,
  //           oldest_blob_file_number, oldest_ancester_time, file_creation_time,
  //           file_checksum, file_checksum_func_name, min_timestamp,
  //           max_timestamp, unique_id);
  //   AddTableFile(table_file, file_path_id, total_entries, 0/* tablefile中的sub_file_size无用*/, smallest, largest,
  //                smallest_seqno, largest_seqno, marked_for_compaction,
  //                temperature, oldest_blob_file_number, oldest_ancester_time,
  //                file_creation_time, file_checksum, file_checksum_func_name,
  //                min_timestamp, max_timestamp, unique_id);
    
  // }

  void AddGuard(int level, const std::string& guard) {
    new_guard_.insert(std::make_pair(level,guard));
  }

  void DeleteGuard(int level, const std::string& guard) {
    deleted_guard_.insert(std::make_pair(level,guard));
  }

  // WQTODO 未来要不要设计delete guard 参考跳表的删除？
  // Retrieve the table files added as well as their associated levels.
  using NewFiles = std::vector<std::pair<int, FileMetaData>>;
  using NewTableFiles = std::vector<FileMetaData>;
  //此处vector中应该用set去重
  using NewGuards = std::set<std::pair<int, std::string>>;
  using DeletedGuards = std::set<std::pair<int, std::string>>;
  const NewFiles& GetNewFiles() const { return new_files_; }
  const NewTableFiles& GetNewTableFiles() const { return new_table_files_; }
  const NewGuards& GetNewGuards() const { return new_guard_; }
  const DeletedGuards& GetDeletedGuards() const { return deleted_guard_; }
  // Retrieve all the compact cursors
  using CompactCursors = std::vector<std::pair<int, InternalKey>>;
  const CompactCursors& GetCompactCursors() const { return compact_cursors_; }
  void AddCompactCursor(int level, const InternalKey& cursor) {
    compact_cursors_.push_back(std::make_pair(level, cursor));
  }
  void SetCompactCursors(
      const std::vector<InternalKey>& compact_cursors_by_level) {
    compact_cursors_.clear();
    compact_cursors_.reserve(compact_cursors_by_level.size());
    for (int i = 0; i < (int)compact_cursors_by_level.size(); i++) {
      if (compact_cursors_by_level[i].Valid()) {
        compact_cursors_.push_back(
            std::make_pair(i, compact_cursors_by_level[i]));
      }
    }
  }

  // Add a new blob file.
  void AddBlobFile(uint64_t blob_file_number, uint64_t total_blob_count,
                   uint64_t total_blob_bytes, std::string checksum_method,
                   std::string checksum_value) {
    blob_file_additions_.emplace_back(
        blob_file_number, total_blob_count, total_blob_bytes,
        std::move(checksum_method), std::move(checksum_value));
  }

  void AddBlobFile(BlobFileAddition blob_file_addition) {
    blob_file_additions_.emplace_back(std::move(blob_file_addition));
  }

  // Retrieve all the blob files added.
  using BlobFileAdditions = std::vector<BlobFileAddition>;
  const BlobFileAdditions& GetBlobFileAdditions() const {
    return blob_file_additions_;
  }

  void SetBlobFileAdditions(BlobFileAdditions blob_file_additions) {
    assert(blob_file_additions_.empty());
    blob_file_additions_ = std::move(blob_file_additions);
  }

  // Add garbage for an existing blob file.  Note: intentionally broken English
  // follows.
  void AddBlobFileGarbage(uint64_t blob_file_number,
                          uint64_t garbage_blob_count,
                          uint64_t garbage_blob_bytes) {
    blob_file_garbages_.emplace_back(blob_file_number, garbage_blob_count,
                                     garbage_blob_bytes);
  }

  void AddBlobFileGarbage(BlobFileGarbage blob_file_garbage) {
    blob_file_garbages_.emplace_back(std::move(blob_file_garbage));
  }

  // Retrieve all the blob file garbage added.
  using BlobFileGarbages = std::vector<BlobFileGarbage>;
  const BlobFileGarbages& GetBlobFileGarbages() const {
    return blob_file_garbages_;
  }

  void SetBlobFileGarbages(BlobFileGarbages blob_file_garbages) {
    assert(blob_file_garbages_.empty());
    blob_file_garbages_ = std::move(blob_file_garbages);
  }

  // Add a WAL (either just created or closed).
  // AddWal and DeleteWalsBefore cannot be called on the same VersionEdit.
  void AddWal(WalNumber number, WalMetadata metadata = WalMetadata()) {
    assert(NumEntries() == wal_additions_.size());
    wal_additions_.emplace_back(number, std::move(metadata));
  }

  // Retrieve all the added WALs.
  const WalAdditions& GetWalAdditions() const { return wal_additions_; }

  bool IsWalAddition() const { return !wal_additions_.empty(); }

  // Delete a WAL (either directly deleted or archived).
  // AddWal and DeleteWalsBefore cannot be called on the same VersionEdit.
  void DeleteWalsBefore(WalNumber number) {
    assert((NumEntries() == 1) == !wal_deletion_.IsEmpty());
    wal_deletion_ = WalDeletion(number);
  }

  const WalDeletion& GetWalDeletion() const { return wal_deletion_; }

  bool IsWalDeletion() const { return !wal_deletion_.IsEmpty(); }

  bool IsWalManipulation() const {
    size_t entries = NumEntries();
    return (entries > 0) && ((entries == wal_additions_.size()) ||
                             (entries == !wal_deletion_.IsEmpty()));
  }

  // Number of edits
  size_t NumEntries() const {
    return new_files_.size() + deleted_files_.size() +
           blob_file_additions_.size() + blob_file_garbages_.size() +
           wal_additions_.size() + !wal_deletion_.IsEmpty();
  }

  void SetColumnFamily(uint32_t column_family_id) {
    column_family_ = column_family_id;
  }
  uint32_t GetColumnFamily() const { return column_family_; }

  // set column family ID by calling SetColumnFamily()
  void AddColumnFamily(const std::string& name) {
    assert(!is_column_family_drop_);
    assert(!is_column_family_add_);
    assert(NumEntries() == 0);
    is_column_family_add_ = true;
    column_family_name_ = name;
  }

  // set column family ID by calling SetColumnFamily()
  void DropColumnFamily() {
    assert(!is_column_family_drop_);
    assert(!is_column_family_add_);
    assert(NumEntries() == 0);
    is_column_family_drop_ = true;
  }

  bool IsColumnFamilyManipulation() const {
    return is_column_family_add_ || is_column_family_drop_;
  }

  bool IsColumnFamilyAdd() const { return is_column_family_add_; }

  bool IsColumnFamilyDrop() const { return is_column_family_drop_; }

  void MarkAtomicGroup(uint32_t remaining_entries) {
    is_in_atomic_group_ = true;
    remaining_entries_ = remaining_entries;
  }
  bool IsInAtomicGroup() const { return is_in_atomic_group_; }
  uint32_t GetRemainingEntries() const { return remaining_entries_; }

  bool HasFullHistoryTsLow() const { return !full_history_ts_low_.empty(); }
  const std::string& GetFullHistoryTsLow() const {
    assert(HasFullHistoryTsLow());
    return full_history_ts_low_;
  }
  void SetFullHistoryTsLow(std::string full_history_ts_low) {
    assert(!full_history_ts_low.empty());
    full_history_ts_low_ = std::move(full_history_ts_low);
  }

  // return true on success.
  bool EncodeTo(std::string* dst) const;
  Status DecodeFrom(const Slice& src);

  std::string DebugString(bool hex_key = false) const;
  std::string DebugJSON(int edit_num, bool hex_key = false) const;

 private:
  friend class ReactiveVersionSet;
  friend class VersionEditHandlerBase;
  friend class ListColumnFamiliesHandler;
  friend class VersionEditHandler;
  friend class VersionEditHandlerPointInTime;
  friend class DumpManifestHandler;
  friend class VersionSet;
  friend class Version;
  friend class AtomicGroupReadBuffer;

  bool GetLevel(Slice* input, int* level, const char** msg);

  const char* DecodeNewFile4From(Slice* input);

  int max_level_ = 0;
  std::string db_id_;
  std::string comparator_;
  uint64_t log_number_ = 0;
  uint64_t prev_log_number_ = 0;
  uint64_t next_file_number_ = 0;
  uint32_t max_column_family_ = 0;
  // The most recent WAL log number that is deleted
  uint64_t min_log_number_to_keep_ = 0;
  SequenceNumber last_sequence_ = 0;
  bool has_db_id_ = false;
  bool has_comparator_ = false;
  bool has_log_number_ = false;
  bool has_prev_log_number_ = false;
  bool has_next_file_number_ = false;
  bool has_max_column_family_ = false;
  bool has_min_log_number_to_keep_ = false;
  bool has_last_sequence_ = false;

  // Compaction cursors for round-robin compaction policy
  CompactCursors compact_cursors_;

  DeletedFiles deleted_files_;
  NewFiles new_files_;
  // 这里不会设计DeletedTableFiles，因为TableFiles的删除不依赖于这个，而是依赖于files的新增和删除。
  NewTableFiles new_table_files_;
  
  NewGuards new_guard_;
  
  DeletedGuards deleted_guard_; //WQTODO 不一定有用

  BlobFileAdditions blob_file_additions_;
  BlobFileGarbages blob_file_garbages_;

  WalAdditions wal_additions_;
  WalDeletion wal_deletion_;

  // Each version edit record should have column_family_ set
  // If it's not set, it is default (0)
  uint32_t column_family_ = 0;
  // a version edit can be either column_family add or
  // column_family drop. If it's column family add,
  // it also includes column family name.
  bool is_column_family_drop_ = false;
  bool is_column_family_add_ = false;
  std::string column_family_name_;

  bool is_in_atomic_group_ = false;
  uint32_t remaining_entries_ = 0;

  std::string full_history_ts_low_;
};

}  // namespace ROCKSDB_NAMESPACE
