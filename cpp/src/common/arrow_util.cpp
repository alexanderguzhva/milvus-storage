// Copyright 2023 Zilliz
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "common/arrow_util.h"
#include "common/macro.h"

namespace milvus_storage {
Result<std::unique_ptr<parquet::arrow::FileReader>> MakeArrowFileReader(arrow::fs::FileSystem& fs,
                                                                        const std::string& file_path) {
  ASSIGN_OR_RETURN_ARROW_NOT_OK(auto file, fs.OpenInputFile(file_path));

  std::unique_ptr<parquet::arrow::FileReader> file_reader;
  RETURN_ARROW_NOT_OK(parquet::arrow::OpenFile(file, arrow::default_memory_pool(), &file_reader));
  return std::move(file_reader);
}

Result<std::unique_ptr<arrow::RecordBatchReader>> MakeArrowRecordBatchReader(
    parquet::arrow::FileReader& reader, const ReadOptions& options) {
  auto metadata = reader.parquet_reader()->metadata();
  std::vector<int> row_group_indices;
  std::vector<int> column_indices;

  for (const auto& column_name : options.columns) {
    auto column_idx = metadata->schema()->ColumnIndex(column_name);
    column_indices.emplace_back(column_idx);
  }
  for (const auto& filter : options.filters) {
    auto column_idx = metadata->schema()->ColumnIndex(filter->get_column_name());
    column_indices.emplace_back(column_idx);
  }

  for (int i = 0; i < metadata->num_row_groups(); ++i) {
    auto row_group_metadata = metadata->RowGroup(i);
    bool can_ignored = false;

    for (const auto& filter : options.filters) {
      auto column_idx = metadata->schema()->ColumnIndex(filter->get_column_name());
      auto column_meta = row_group_metadata->ColumnChunk(column_idx);
      auto stats = column_meta->statistics();

      if (stats == nullptr || !stats->HasMinMax()) {
        continue;
      }
      if (filter->CheckStatistics(stats.get())) {
        can_ignored = true;
        break;
      }
    }
    if (!can_ignored) {
      row_group_indices.emplace_back(i);
    }
  }
  std::unique_ptr<arrow::RecordBatchReader> record_reader;

  // RETURN_ARROW_NOT_OK(reader->GetRecordBatchReader(row_group_indices, column_indices, &record_reader));
  RETURN_ARROW_NOT_OK(reader.GetRecordBatchReader(row_group_indices, &record_reader));
  return record_reader;
}

}  // namespace milvus_storage
