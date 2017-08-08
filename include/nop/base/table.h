#ifndef LIBNOP_INCLUDE_NOP_BASE_TABLE_H_
#define LIBNOP_INCLUDE_NOP_BASE_TABLE_H_

#include <type_traits>

#include <nop/base/encoding.h>
#include <nop/base/macros.h>
#include <nop/base/members.h>
#include <nop/base/optional.h>
#include <nop/base/utility.h>
#include <nop/utility/bounded_reader.h>
#include <nop/utility/bounded_writer.h>
#include <nop/utility/sip_hash.h>

namespace nop {

//
// Tables are bi-directional binary compatible structures that support
// serializing and deserializing data from different versions of the same type.
// Use a table type when maintaining compatability between different versions of
// serialized data is important to the application. However, consider that every
// non-empty table entry cost at least two bytes more than the underlying type
// encoding.
//
// Users define tables using classes or structures with members of type
// Entry<T, Id>. Entry<T, Id> is similar to Optional<T> in that an entry may
// either be empty or contain a value of type T. Entries that are empty are not
// encoded during serialization to save space. Programs using tables should
// handle empty values in a sensible way, ensuring that missing entries in older
// data are handled gracefully.
//
// Example of a simple table definition:
//
// struct MyTable {
//   Entry<Address, 0> address;
//   Entry<PhoneNumber, 1> phone_number;
//   NOP_TABLE("MyTable", MyTable, address, phone_number);
// };
//
// Example of handling empty values:
//
// MyTable my_table;
// auto status = serializer.Read(&my_table);
// if (!status)
//   return status;
//
// if (my_table.address)
//   HandleAddress(my_table.address.get());
// if (my_table.phone_number)
//   HandlePhoneNumber(my_table.phone_number.get());
//
// Table entries may be public, private, or protected, depending on how the
// table type will be used. If the entries are non-public, call NOP_TABLE() in a
// private section to avoid leaking member pointers to arbitrary code.
//
// Use the following rules to maximize compatability between different versions
// of a table type:
//   1. Always use unique values for Id when adding an entry to a table. Never
//      reuse a previously used Id in the same table.
//   2. When deprecating an entry use the DeletedEntry type instead of deleting
//      the entry entirely to document the deprecation and prevent resuse of an
//      old entry id.
//   3. Never change the Id for an entry. Doing so will break compatibility with
//      older versions of serialized data.
//   4. Never change the string name passed as the first argument to the macro
//      NOP_TABLE. This is used to compute a hash used for sanity checking
//      during deserialization. Changing this string will break compatibility
//      with older versions of serialized data.
//

// Type tags to define used and deprecated entries.
struct ActiveEntry {};
struct DeletedEntry {};

// Base type of table entries.
template <typename, std::uint64_t, typename = ActiveEntry>
struct Entry;

// Specialization of Entry for active/used entries. Each non-empty, active entry
// is encoded during serialization.
template <typename T, std::uint64_t Id_>
struct Entry<T, Id_, ActiveEntry> : Optional<T> {
  enum : std::uint64_t { Id = Id_ };
  using Optional<T>::Optional;
};

// Specialization of Entry for deleted/deprecated entries. These entries are
// always empty and are never encoded. When encountered during deserialization
// these entries are ignored.
template <typename T, std::uint64_t Id_>
struct Entry<T, Id_, DeletedEntry> {
  enum : std::uint64_t { Id = Id_ };

  bool empty() const { return true; }
  explicit operator bool() const { return !empty(); }
  void clear() {}
};

// Determines whether two entries have the same id.
template <typename A, typename B>
struct SameEntryId : std::integral_constant<bool, A::Id == B::Id> {};

// Similar to MemberList used for serializable structures/classes. This type
// also records the hash of the table name for sanity checking during
// deserialization.
template <typename HashValue, typename... MemberPointers>
struct EntryList : MemberList<MemberPointers...> {
  static_assert(IsUnique<SameEntryId, typename MemberPointers::Type...>::value,
                "Entry ids must be unique.");
  enum : std::uint64_t { Hash = HashValue::Value };
};

// Determines whether the given type T has a nested type named NOP__ENTRIES that
// is a subtype of EntryList. This type evaluates to std::true_type for a
// properly defined table type, std::false_type otherwise.
template <typename, typename = void>
struct HasEntryList : std::false_type {};
template <typename T>
struct HasEntryList<T, Void<typename T::NOP__ENTRIES>>
    : std::integral_constant<
          bool, IsTemplateBaseOf<EntryList, typename T::NOP__ENTRIES>::value> {
};

// Enable if HasEntryType<T> evaluates to std::true_type.
template <typename T>
using EnableIfHasEntryList =
    typename std::enable_if<HasEntryList<T>::value>::type;

// Traits type that retrieves the NOP__ENTRIES traits type for the given type T.
template <typename T>
struct EntryListTraits {
  using EntryList = typename T::NOP__ENTRIES;
};

// SipHash keys used to compute the table hash of the given table name string.
enum : std::uint64_t {
  kNopTableKey0 = 0xbaadf00ddeadbeef,
  kNopTableKey1 = 0x0123456789abcdef,
};

// Defines a table type, its hash, and its members. This macro should be call
// once within a table struct or class to inform the serialization engine about
// the table members and hash value. This is accomplished by befriending several
// key classes and defining an internal type named NOP__ENTRIES that describes
// the table type's Entry<T, Id> members.
#define NOP_TABLE(string_name, type, ... /*entries*/)                \
  template <typename, typename>                                      \
  friend struct ::nop::Encoding;                                     \
  template <typename, typename>                                      \
  friend struct ::nop::HasEntryList;                                 \
  template <typename>                                                \
  friend struct ::nop::EntryListTraits;                              \
  using NOP__ENTRIES = ::nop::EntryList<                             \
      ::nop::HashValue<::nop::SipHash::Compute(                      \
          string_name, ::nop::kNopTableKey0, ::nop::kNopTableKey1)>, \
      NOP_MEMBER_LIST(type, __VA_ARGS__)>

//
// Entry<T, Id, ActiveEntry> encoding format:
//
// +----------+-----+------------+-------+---------+
// | INT64:ID | BIN | INT64:SIZE | VALUE | PADDING |
// +----------+-----+------------+-------+---------+
//
// VALUE must be a valid encoding of type T. If the entry is empty it is not
// encoded. The encoding of type T is wrapped in a sized binary encoding to
// allow deserialization to skip unknown entry types without parsing the full
// encoded entry. SIZE is equal to the total number of bytes in VALUE and
// PADDING.
//
// Entry<T, Id, DeletedEntry> encoding format:
//
// +----------+-----+------------+--------------+
// | INT64:ID | BIN | INT64:SIZE | OPAQUE BYTES |
// +----------+-----+------------+--------------+
//
// A deleted entry is never written, but may be encountered by code using newer
// table definitions to read older data streams.
//
// Table encoding format:
//
// +-----+---------+-----------+
// | TAB | INT64:N | N ENTRIES |
// +-----+---------+-----------+
//
// Where N is the number of non-empty, active entries in the table. Older code
// may encounter unknown entry ids when reading data from newer table
// definitions.
//

template <typename Table>
struct Encoding<Table, EnableIfHasEntryList<Table>> : EncodingIO<Table> {
  static constexpr EncodingByte Prefix(const Table& value) {
    return EncodingByte::Table;
  }

  static constexpr std::size_t Size(const Table& value) {
    return BaseEncodingSize(Prefix(value)) +
           Encoding<std::uint64_t>::Size(
               EntryListTraits<Table>::EntryList::Hash) +
           Encoding<std::uint64_t>::Size(
               ActiveEntryCount(value, Index<Count>{})) +
           Size(value, Index<Count>{});
  }

  static constexpr bool Match(EncodingByte prefix) {
    return prefix == EncodingByte::Table;
  }

  template <typename Writer>
  static Status<void> WritePayload(EncodingByte prefix, const Table& value,
                                   Writer* writer) {
    auto status = Encoding<std::uint64_t>::Write(
        EntryListTraits<Table>::EntryList::Hash, writer);
    if (!status)
      return status;

    status = Encoding<std::uint64_t>::Write(
        ActiveEntryCount(value, Index<Count>{}), writer);
    if (!status)
      return status;

    return WriteEntries(value, writer, Index<Count>{});
  }

  template <typename Reader>
  static Status<void> ReadPayload(EncodingByte prefix, Table* value,
                                  Reader* reader) {
    // Clear entries so that we can detect whether there are duplicate entries
    // for the same id during deserialization.
    ClearEntries(value, Index<Count>{});

    std::uint64_t hash;
    auto status = Encoding<std::uint64_t>::Read(&hash, reader);
    if (!status)
      return status;
    else if (hash != EntryListTraits<Table>::EntryList::Hash)
      return ErrorStatus(EPROTO);

    std::uint64_t count;
    status = Encoding<std::uint64_t>::Read(&count, reader);
    if (!status)
      return status;

    return ReadEntries(value, count, reader);
  }

 private:
  enum : std::size_t { Count = EntryListTraits<Table>::EntryList::Count };

  template <std::size_t Index>
  using PointerAt =
      typename EntryListTraits<Table>::EntryList::template At<Index>;

  static constexpr std::size_t ActiveEntryCount(const Table& value, Index<0>) {
    return 0;
  }

  template <std::size_t index>
  static constexpr std::size_t ActiveEntryCount(const Table& value,
                                                Index<index>) {
    using Pointer = PointerAt<index - 1>;
    const std::size_t count = Pointer::Resolve(value) ? 1 : 0;
    return ActiveEntryCount(value, Index<index - 1>{}) + count;
  }

  template <typename T, std::uint64_t Id>
  static constexpr std::size_t Size(const Entry<T, Id, ActiveEntry>& entry) {
    if (entry)
      return Encoding<std::uint64_t>::Size(Id) + Encoding<T>::Size(entry.get());
    else
      return 0;
  }

  template <typename T, std::uint64_t Id>
  static constexpr std::size_t Size(
      const Entry<T, Id, DeletedEntry>& /*entry*/) {
    return 0;
  }

  static constexpr std::size_t Size(const Table& value, Index<0>) { return 0; }

  template <std::size_t index>
  static constexpr std::size_t Size(const Table& value, Index<index>) {
    using Pointer = PointerAt<index - 1>;
    return Size(value, Index<index - 1>{}) + Size(Pointer::Resolve(value));
  }

  static void ClearEntries(Table* value, Index<0>) {}

  template <std::size_t index>
  static void ClearEntries(Table* value, Index<index>) {
    ClearEntries(value, Index<index - 1>{});
    PointerAt<index - 1>::Resolve(value)->clear();
  }

  template <typename T, std::uint64_t Id, typename Writer>
  static Status<void> WriteEntry(const Entry<T, Id, ActiveEntry>& entry,
                                 Writer* writer) {
    if (entry) {
      auto status = Encoding<std::uint64_t>::Write(Id, writer);
      if (!status)
        return status;

      status = writer->Write(EncodingByte::Binary);
      if (!status)
        return status;

      const std::uint64_t size = Encoding<T>::Size(entry.get());
      status = Encoding<std::uint64_t>::Write(size, writer);
      if (!status)
        return status;

      // Use a BoundedWriter to track the number of bytes written. Since a few
      // encodings overestimate their size, the remaining bytes must be padded
      // out to match the size written above. This is a tradeoff that
      // potentially increases the encoding size to avoid unnecessary dynamic
      // memory allocation during encoding; some size savings could be made by
      // encoding the entry to a temporary buffer and then writing the exact
      // size for the binary container. However, overestimation is rare and
      // small, making the savings not worth the expense of the temporary
      // buffer.
      BoundedWriter<Writer> bounded_writer{writer, size};
      status = Encoding<T>::Write(entry.get(), &bounded_writer);
      if (!status)
        return status;

      return bounded_writer.WritePadding();
    } else {
      return {};
    }
  }

  template <typename T, std::uint64_t Id, typename Writer>
  static Status<void> WriteEntry(const Entry<T, Id, DeletedEntry>& /*entry*/,
                                 Writer* /*writer*/) {
    return {};
  }

  template <typename Writer>
  static Status<void> WriteEntries(const Table& /*value*/, Writer* /*writer*/,
                                   Index<0>) {
    return {};
  }

  template <std::size_t index, typename Writer>
  static Status<void> WriteEntries(const Table& value, Writer* writer,
                                   Index<index>) {
    auto status = WriteEntries(value, writer, Index<index - 1>{});
    if (!status)
      return status;

    using Pointer = PointerAt<index - 1>;
    return WriteEntry(Pointer::Resolve(value), writer);
  }

  template <typename T, std::uint64_t Id, typename Reader>
  static Status<void> ReadEntry(Entry<T, Id, ActiveEntry>* entry,
                                Reader* reader) {
    // At the beginning of reading the table the destination entries are
    // cleared. If an entry is not cleared here then more than one entry for
    // the same id was written in violation of the table protocol.
    if (entry->empty()) {
      EncodingByte prefix;
      auto status = reader->Read(&prefix);
      if (!status)
        return status;
      else if (prefix != EncodingByte::Binary)
        return ErrorStatus(EPROTO);

      std::uint64_t size;
      status = Encoding<std::uint64_t>::Read(&size, reader);
      if (!status)
        return status;

      // Default construct the entry;
      *entry = T{};

      // Use a BoundedReader to handle any padding that might follow the
      // value and catch invalid sizes while decoding inside the binary
      // container.
      BoundedReader<Reader> bounded_reader{reader, size};
      status = Encoding<T>::Read(&entry->get(), &bounded_reader);
      if (!status)
        return status;

      return bounded_reader.ReadPadding();
    } else {
      return ErrorStatus(EPROTO);
    }
  }

  // Skips over the binary container for an entry.
  template <typename Reader>
  static Status<void> SkipEntry(Reader* reader) {
    EncodingByte prefix;
    auto status = reader->Read(&prefix);
    if (!status)
      return status;
    else if (prefix != EncodingByte::Binary)
      return ErrorStatus(EPROTO);

    std::uint64_t size;
    status = Encoding<std::uint64_t>::Read(&size, reader);
    if (!status)
      return status;

    return reader->Skip(size);
  }

  template <typename T, std::uint64_t Id, typename Reader>
  static Status<void> ReadEntry(Entry<T, Id, DeletedEntry>* /*entry*/,
                                Reader* reader) {
    return SkipEntry(reader);
  }

  template <typename Reader>
  static Status<void> ReadEntryForId(Table* /*value*/, std::uint64_t /*id*/,
                                     Reader* reader, Index<0>) {
    return SkipEntry(reader);
  }

  template <typename Reader, std::size_t index>
  static Status<void> ReadEntryForId(Table* value, std::uint64_t id,
                                     Reader* reader, Index<index>) {
    using Pointer = PointerAt<index - 1>;
    using Type = typename Pointer::Type;
    if (Type::Id == id)
      return ReadEntry(Pointer::Resolve(value), reader);
    else
      return ReadEntryForId(value, id, reader, Index<index - 1>{});
  }

  template <typename Reader>
  static Status<void> ReadEntries(Table* value, std::uint64_t count,
                                  Reader* reader) {
    for (std::uint64_t i = 0; i < count; i++) {
      std::uint64_t id;
      auto status = Encoding<std::uint64_t>::Read(&id, reader);
      if (!status)
        return status;

      status = ReadEntryForId(value, id, reader, Index<Count>{});
      if (!status)
        return status;
    }
    return {};
  }
};

}  // namespace nop

#endif  // LIBNOP_INCLUDE_NOP_BASE_TABLE_H_
