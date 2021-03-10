/*
 * Copyright 2021 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Firestore/core/src/model/object_value.h"

#include <set>

#include "Firestore/Protos/nanopb/google/firestore/v1/document.nanopb.h"
#include "Firestore/core/src/model/field_path.h"
#include "Firestore/core/src/model/values.h"
#include "Firestore/core/src/nanopb/message.h"
#include "Firestore/core/src/nanopb/nanopb_util.h"

namespace firebase {
namespace firestore {
namespace model {

MutableObjectValue::MutableObjectValue() {
  value_.which_value_type = google_firestore_v1_Value_map_value_tag;
  value_.map_value.fields_count = 0;
  value_.map_value.fields =
      nanopb::MakeArray<google_firestore_v1_MapValue_FieldsEntry>(0);
}

model::FieldMask MutableObjectValue::ToFieldMask() const {
  return ExtractFieldMask(value_.map_value);
}

model::FieldMask MutableObjectValue::ExtractFieldMask(
    const google_firestore_v1_MapValue& value) const {
  std::set<FieldPath> fields;
  for (size_t i = 0; i < value.fields_count; ++i) {
    FieldPath current_path({nanopb::MakeString(value.fields[i].key)});
    if (value.fields[i].value.which_value_type ==
        google_firestore_v1_Value_map_value_tag) {
      model::FieldMask nested_mask =
          ExtractFieldMask(value.fields[i].value.map_value);
      if (nested_mask.begin() == nested_mask.end()) {
        // Preserve the empty map by adding it to the FieldMask.
        fields.insert(current_path);
      } else {
        for (const FieldPath& nested_path : nested_mask) {
          fields.insert(current_path.Append(nested_path));
        }
      }
    } else {
      fields.insert(current_path);
    }
  }
  return model::FieldMask(fields);
}

absl::optional<google_firestore_v1_Value> MutableObjectValue::Get(
    const firebase::firestore::model::FieldPath& path) const {
  if (path.empty()) {
    return value_;
  } else {
    google_firestore_v1_Value nested_value = value_;
    for (const std::string& segment : path) {
      if (nested_value.which_value_type !=
          google_firestore_v1_Value_map_value_tag) {
        return {};
      }

      _google_firestore_v1_MapValue_FieldsEntry* entry =
          FindMapEntry(nested_value.map_value, segment);

      if (!entry) return {};

      nested_value = entry->value;
    }

    return nested_value;
  }
}

void MutableObjectValue::Set(const model::FieldPath& path,
                             const google_firestore_v1_Value& value) {
  HARD_ASSERT(!path.empty(), "Cannot set field for empty path on ObjectValue");

  google_firestore_v1_MapValue* parent = &value_.map_value;

  // Find a or create a parent map entry for `value`.
  for (const std::string& segment : path.PopLast()) {
    _google_firestore_v1_MapValue_FieldsEntry* entry =
        FindMapEntry(*parent, segment);

    if (entry) {
      if (entry->value.which_value_type !=
          google_firestore_v1_Value_map_value_tag) {
        nanopb::FreeNanopbMessage(google_firestore_v1_Value_fields,
                                  &entry->value);
        entry->value.which_value_type = google_firestore_v1_Value_map_value_tag;
      }

      parent = &entry->value.map_value;
    } else {
      ResizeMapValue(
          parent, parent->fields_count + 1,
          [](_google_firestore_v1_MapValue_FieldsEntry*) { return true; });

      _google_firestore_v1_MapValue_FieldsEntry* last_entry =
          &parent->fields[parent->fields_count - 1];
      last_entry->key = nanopb::MakeBytesArray(segment);
      last_entry->value.which_value_type =
          google_firestore_v1_Value_map_value_tag;
      parent = &last_entry->value.map_value;
    }
  }

  // Add the value to its immediate parent
  _google_firestore_v1_MapValue_FieldsEntry* entry =
      FindMapEntry(*parent, path.last_segment());
  if (entry) {
    // Overwrite the current entry
    nanopb::FreeNanopbMessage(google_firestore_v1_Value_fields, &entry->value);
    entry->value = value;
  } else {
    // Add a new entry
    pb_size_t existing_count = parent->fields_count;
    ResizeMapValue(
        parent, existing_count + 1,
        [](_google_firestore_v1_MapValue_FieldsEntry*) { return true; });

    parent->fields[existing_count].key =
        nanopb::MakeBytesArray(path.last_segment());
    parent->fields[existing_count].value = value;
  }
}

void MutableObjectValue::SetAll(const model::FieldMask& field_mask,
                                const MutableObjectValue& data) {
  for (const FieldPath& path : field_mask) {
    absl::optional<google_firestore_v1_Value> value = data.Get(path);
    if (value) {
      Set(path, *value);
    } else {
      Delete(path);
    }
  }
}

void MutableObjectValue::Delete(const FieldPath& path) {
  HARD_ASSERT(!path.empty(), "Cannot set field for empty path on ObjectValue");

  google_firestore_v1_MapValue* parent = &value_.map_value;
  for (const std::string& segment : path.PopLast()) {
    _google_firestore_v1_MapValue_FieldsEntry* entry =
        FindMapEntry(*parent, segment);
    if (!entry || entry->value.which_value_type !=
                      google_firestore_v1_Value_map_value_tag) {
      // Exit early since the entry does not exist
      return;
    }

    parent = &entry->value.map_value;
  }

  _google_firestore_v1_MapValue_FieldsEntry* entry =
      FindMapEntry(*parent, path.last_segment());

  if (entry) {
    ResizeMapValue(
        parent, parent->fields_count - 1,
        [entry](google_firestore_v1_MapValue_FieldsEntry* existing_entry) {
          return existing_entry != entry;
        });
  }
}

void MutableObjectValue::ResizeMapValue(
    google_firestore_v1_MapValue* map_value,
    pb_size_t target_size,
    const std::function<bool(google_firestore_v1_MapValue_FieldsEntry*)>&
        filter_fn) const {
  pb_size_t source_size = map_value->fields_count;
  _google_firestore_v1_MapValue_FieldsEntry* existing_entries =
      map_value->fields;

  map_value->fields_count = target_size;
  map_value->fields =
      nanopb::MakeArray<google_firestore_v1_MapValue_FieldsEntry>(target_size);

  for (pb_size_t target_index = 0, source_index = 0;
       target_index < target_size && source_index < source_size;
       ++source_index) {
    if (filter_fn(&existing_entries[source_index])) {
      map_value->fields[target_index] = existing_entries[source_index];
      ++target_index;
    } else {
      nanopb::FreeNanopbMessage(google_firestore_v1_Value_fields,
                                &existing_entries[source_index]);
    }
  }

  nanopb::FreeNanopbMessage(google_firestore_v1_MapValue_FieldsEntry_fields,
                            &existing_entries);
}

_google_firestore_v1_MapValue_FieldsEntry* MutableObjectValue::FindMapEntry(
    const google_firestore_v1_MapValue& map_value, const std::string& segment) {
  for (size_t i = 0; i < map_value.fields_count; ++i) {
    if (nanopb::MakeStringView(map_value.fields[i].key) == segment) {
      return &map_value.fields[i];
    }
  }
  return nullptr;
}

}  // namespace model
}  // namespace firestore
}  // namespace firebase
