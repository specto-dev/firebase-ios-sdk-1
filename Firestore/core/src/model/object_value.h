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

#ifndef FIRESTORE_CORE_SRC_MODEL_OBJECT_VALUE_H_
#define FIRESTORE_CORE_SRC_MODEL_OBJECT_VALUE_H_

#include <string>
#include <ostream>
#include <unordered_map>

#include "Firestore/Protos/nanopb/google/firestore/v1/document.nanopb.h"
#include "Firestore/core/src/model/field_mask.h"
#include "Firestore/core/src/model/field_path.h"
#include "Firestore/core/src/model/values.h"
#include "absl/types/optional.h"

namespace firebase {
namespace firestore {
namespace model {

/** A structured object value stored in Firestore. */
class MutableObjectValue {  // TODO(mrschmidt): Rename to ObjectValue
 public:
  MutableObjectValue();

  explicit MutableObjectValue(google_firestore_v1_Value value) : value_(value) {
    HARD_ASSERT(
        value.which_value_type == google_firestore_v1_Value_map_value_tag,
        "ObjectValues should be backed by a MapValue");
  }

  /** Recursively extracts the FieldPaths that are set in this ObjectValue. */
  FieldMask ToFieldMask() const;

  /**
   * Returns the value at the given path or null.
   *
   * @param fieldPath the path to search
   * @return The value at the path or null if it doesn't exist.
   */
  absl::optional<google_firestore_v1_Value> Get(const FieldPath& path) const;

  /**
   * Removes the field at the specified path. If there is no field at the
   * specified path nothing is changed.
   *
   * @param path The field path to remove
   */
  void Delete(const FieldPath& path);

  /**
   * Sets the field to the provided value.
   *
   * @param path The field path to set.
   * @param value The value to set.
   */
  void Set(const FieldPath& path, const google_firestore_v1_Value& value);

  /**
   * Sets the provided fields to the provided values. Only fields included in
   * `field_mask` are modified. If a field is included in field_mask, but
   * missing in `data`, it is deleted.
   *
   * @param field_mask The field mask that controls which fields to modify.
   * @param data An ObjectValue that contains the field values.
   */
  void SetAll(const model::FieldMask& field_mask,
              const MutableObjectValue& data);

  friend bool operator==(const MutableObjectValue& lhs,
                         const MutableObjectValue& rhs);
  friend std::ostream& operator<<(std::ostream& out,
                                  const MutableObjectValue& object_value);

 private:
  google_firestore_v1_Value value_{};

  /** Returns the field mask for the provided map value. */
  model::FieldMask ExtractFieldMask(
      const google_firestore_v1_MapValue& value) const;

  /** Finds an entry by key in the provided map value. Runs in O(1) */
  static _google_firestore_v1_MapValue_FieldsEntry* FindMapEntry(
      const google_firestore_v1_MapValue& map_value,
      const std::string& segment);

  /**
   *  Resizes the field array of the provided map value. The map is resized
   *  to `target_size` and all values that match `filter_fn` are copied over.
   *  The result is padded with empty values or truncated if `filter_fn` does
   *  not produce `target_size`.
   *
   *  @param map_value The map whose underlying field array we should resize.
   *  @param target_size The new sie of the field array.
   *  @param filter_fn A function that operates on the existing map entries and
   *  is used to determine whether entries should be copied to the new field
   * array.
   */
  void ResizeMapValue(
      google_firestore_v1_MapValue* map_value,
      pb_size_t target_size,
      const std::function<bool(google_firestore_v1_MapValue_FieldsEntry*)>&
          filter_fn) const;
};

inline bool operator==(const MutableObjectValue& lhs,
                       const MutableObjectValue& rhs) {
  return Values::Equals(lhs.value_, rhs.value_);
}

inline std::ostream& operator<<(std::ostream& out,
                                const MutableObjectValue& object_value) {
  return out << Values::CanonicalId(object_value.value_);
}

}  // namespace model
}  // namespace firestore
}  // namespace firebase

#endif  // FIRESTORE_CORE_SRC_MODEL_OBJECT_VALUE_H_
