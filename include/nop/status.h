/*
 * Copyright 2017 The Native Object Protocols Authors
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

#ifndef LIBNOP_INCLUDE_NOP_STATUS_H_
#define LIBNOP_INCLUDE_NOP_STATUS_H_

#include <string>

#include <nop/types/result.h>

//
// Status<T> is the return type used by the serialization engine to return
// either success and a value or an error indicating the nature of the failure.
// This type is based on the more general Result<ErrorEnum, T>.
//

namespace nop {

enum class ErrorStatus {
  None,
  UnexpectedEncodingType,
  UnexpectedHandleType,
  UnexpectedVariantType,
  InvalidContainerLength,
  InvalidMemberCount,
  InvalidStringLength,
  InvalidTableHash,
  InvalidHandleReference,
  InvalidInterfaceMethod,
  DuplicateTableEntry,
  ReadLimitReached,
  WriteLimitReached,
  StreamError,
  ProtocolError,
  IOError,
  SystemError,
};

template <typename T>
struct Status : Result<ErrorStatus, T> {
  using Result<ErrorStatus, T>::Result;

  std::string GetErrorMessage() const {
    switch (this->error()) {
      case ErrorStatus::None:
        return "No Error";
      case ErrorStatus::UnexpectedEncodingType:
        return "Unexpected Encoding Type";
      case ErrorStatus::UnexpectedHandleType:
        return "Unexpected Handle Type";
      case ErrorStatus::UnexpectedVariantType:
        return "Unexpected Variant Type";
      case ErrorStatus::InvalidContainerLength:
        return "Invalid Container Length";
      case ErrorStatus::InvalidMemberCount:
        return "Invalid Member Count";
      case ErrorStatus::InvalidStringLength:
        return "Invalid String Length";
      case ErrorStatus::InvalidTableHash:
        return "Invalid Table Hash";
      case ErrorStatus::InvalidHandleReference:
        return "Invalid Handle Reference";
      case ErrorStatus::InvalidInterfaceMethod:
        return "Invalid Interface Method";
      case ErrorStatus::DuplicateTableEntry:
        return "Duplicate Table Hash";
      case ErrorStatus::ReadLimitReached:
        return "Read Limit Reached";
      case ErrorStatus::WriteLimitReached:
        return "Write Limit Reached";
      case ErrorStatus::StreamError:
        return "Stream Error";
      case ErrorStatus::ProtocolError:
        return "Protocol Error";
      case ErrorStatus::IOError:
        return "IO Error";
      case ErrorStatus::SystemError:
        return "System Error";
      default:
        return "Unknown Error";
    }
  }
};

}  // namespace nop

#endif  // LIBNOP_INCLUDE_NOP_STATUS_H_
