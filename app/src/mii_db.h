// SPDX-License-Identifier: GPL-3.0-or-later
// The console's Mii database (MiiDatabase.dat, "NFDB"), read and written by the launcher's Mii editor.
// Layout and checksums follow the engine's own Mii service (suyu core/hle/service/mii, GPL), so what the editor writes is what the
// game's Mii service accepts: 100 StoreData records of 0x44 bytes (CoreData 0x30 = 7 packed 32-bit words + a 10-character UTF-16
// name, a random create id, a data CRC16 and a device CRC16), then version 1, the count and a CRC16 of the whole file.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <QByteArray>
#include <QString>

namespace Mii {

constexpr int kMaxMiis = 100;          // the database holds at most this many records
constexpr int kStoreDataSize = 0x44;
constexpr int kDatabaseSize = 0x1A98;
constexpr int kNameLength = 10;

/// One editable value of a Mii: where it sits in the packed words and its range (0..max).
struct Field {
    const char* key;     ///< stable id (ini/json, tests)
    const char* label;   ///< shown in the editor
    const char* group;   ///< editor section
    int word, shift, bits, max;
};
const std::vector<Field>& Fields();
const Field* FindField(const char* key);

struct StoreData {
    std::array<uint8_t, kStoreDataSize> raw{};

    int Get(const Field& f) const;
    void Set(const Field& f, int value);   ///< clamps to the field's range
    int Get(const char* key) const;
    void Set(const char* key, int value);

    QString Name() const;
    void SetName(const QString& name);     ///< at most 10 UTF-16 units; empty becomes "Mii"
    QByteArray CreateId() const;
    void NewCreateId();                    ///< random RFC 4122 v4 id (what a new Mii gets)

    /// Recomputes the data and device checksums; call after any change.
    void UpdateChecksums();
    /// True when the engine's Mii service would accept the record (ranges, name, checksums).
    bool IsValid(QString* why = nullptr) const;
    bool IsSpecial() const { return Get("type") == 1; }

    /// A plain default Mii (the console's "no Mii yet" face), named `name`, with a fresh create id and checksums.
    static StoreData MakeDefault(const QString& name, bool female = false);
};

class Database {
public:
    /// An empty, valid database.
    Database();
    /// Reads a MiiDatabase.dat. Returns nullopt (and why) when the file is missing, has the wrong size, magic, version or checksum.
    static std::optional<Database> Load(const QString& path, QString* error = nullptr);
    static std::optional<Database> FromBytes(const QByteArray& bytes, QString* error = nullptr);
    /// Writes atomically (temporary file, then rename). The checksum is updated first.
    bool Save(const QString& path, QString* error = nullptr);
    QByteArray ToBytes();

    int Count() const { return static_cast<int>(miis.size()); }
    const StoreData& At(int i) const { return miis[static_cast<size_t>(i)]; }
    void Replace(int i, const StoreData& d);
    bool Add(const StoreData& d);          ///< false when the database is full
    void Remove(int i);
    void Move(int from, int to);

private:
    std::vector<StoreData> miis;
};

/// The engine's CRC16 (CCITT, polynomial 0x1021, initial 0), as stored: byte-swapped.
uint16_t Crc16(const uint8_t* data, size_t size);

} // namespace Mii
