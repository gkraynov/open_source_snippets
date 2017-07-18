// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

// Not expected to support:
// - Two's-complement VarInt (int32, int64)
// - Groups
// - Required
// - Packed repeated fields

const ProtoFieldType = {
  UINT: 0,
  SINT: 1,
  STRING: 2,
  FLOAT: 3,
};

const ProtoWireType = {
  VARINT: 0,
  LENGTH_DELIMITED: 2,
  FIXED_32: 5,
  FIXED_64: 1,
}

class ProtobufDecoder {
  constructor(arrayBuffer, offset) {
    this.buffer_ = arrayBuffer;
    this.bytes_ = new Uint8Array(arrayBuffer);
    this.offset_ = offset;
    this.textDecoder_ = new TextDecoder('utf-8');
  }

  readVarInt_() {
    let result = 0;
    let shift = 0;
    for (let pos = this.offset_;; pos++) {
      result |= (this.bytes_[pos] & 0x7F) << shift;
      shift += 7;
      if ((this.bytes_[pos] & 0x80) === 0) {
        this.offset_ = pos + 1;
        return result;
      }
    }
  }

  readZigZag_() {
    const zigZag = this.readVarInt_();
    if (zigZag & 0x01) {
      return -1 - (zigZag >> 1);
    } else {
      return zigZag >> 1;
    }
  }

  readFixed_(size) {
    const dataView = new DataView(this.buffer_, this.offset_, size);
    this.offset_ += size;
    return dataView;
  }

  readString_() {
    const length = this.readVarInt_();
    const rawString = new Uint8Array(this.buffer_, this.offset_, length);
    this.offset_ += length;
    return this.textDecoder_.decode(rawString);
  }

  skipUnknownField_(wireType) {
    if (wireType === ProtoWireType.VARINT) {
      do { this.offset_++; } while (this.bytes_[this.offset_] & 0x80);
    } else if (wireType === ProtoWireType.LENGTH_DELIMITED) {
      const length = this.readVarInt_();
      this.offset_ += length;
    } else if (wireType === ProtoWireType.FIXED_32) {
      this.offset_ += 4;
    } else if (wireType === ProtoWireType.FIXED_64) {
      this.offset_ += 8;
    } else {
      throw 'Unknown wire type.';
    }
  }

  readMessage(length, messageDescriptor) {
    let message = {};
    const stop = this.offset_ + length;
    while (this.offset_ < stop) {
      const preamble = this.readVarInt_();
      const fieldId = preamble >> 3;
      const fieldDescriptor = messageDescriptor[fieldId];
      if (fieldDescriptor === undefined) {
        this.skipUnknownField_(preamble & 0x07);
        continue;
      }

      let value;
      if (fieldDescriptor.type === ProtoFieldType.UINT) {
        value = this.readVarInt_();
      } else if (fieldDescriptor.type === ProtoFieldType.SINT) {
        value = this.readZigZag_();
      } else if (fieldDescriptor.type === ProtoFieldType.STRING) {
        value = this.readString_();
      } else if (fieldDescriptor.type === ProtoFieldType.FLOAT) {
        value = this.readFixed_(4).getFloat32(0, true);
      }
      message[fieldDescriptor.name] = value;
    }
    return message;
  }
};
