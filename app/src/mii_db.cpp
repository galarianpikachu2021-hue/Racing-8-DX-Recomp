// SPDX-License-Identifier: GPL-3.0-or-later
#include "mii_db.h"

#include <QFile>
#include <QRandomGenerator>
#include <QSaveFile>
#include <algorithm>
#include <cstring>

namespace Mii {
namespace {

constexpr uint32_t kMagic = 0x4244464e; // "NFDB"
constexpr int kCoreSize = 0x30;
constexpr int kNameOffset = 0x1c;
constexpr int kCreateIdOffset = 0x30;
constexpr int kDataCrcOffset = 0x40;
constexpr int kDeviceCrcOffset = 0x42;
// The engine's Mii author id (MiiUtil::GetDeviceId: Common::UUID::MakeDefault): the device checksum is computed from it.
constexpr uint8_t kDeviceId[16] = {'E', 'd', 'e', 'n', ' ', 'D', 'e', 'f', 'a', 'u', 'l', 't', ' ', 'U', 'I', 'D'};

uint32_t Word(const std::array<uint8_t, kStoreDataSize>& raw, int w) {
    uint32_t v;
    std::memcpy(&v, raw.data() + 4 * w, 4);
    return v;
}
void SetWord(std::array<uint8_t, kStoreDataSize>& raw, int w, uint32_t v) {
    std::memcpy(raw.data() + 4 * w, &v, 4);
}
void PutCrc(uint8_t* at, uint16_t stored) {
    std::memcpy(at, &stored, 2);
}

uint16_t DeviceCrc16() {
    // MiiUtil::CalculateDeviceCrc16(device id, sizeof(StoreData)), bit for bit.
    int32_t crc = 0;
    for (uint8_t byte : kDeviceId) {
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if ((crc & 0x10000) != 0) crc = crc ^ 0x1021;
        }
        crc ^= byte;
    }
    for (int i = 0; i < kStoreDataSize * 8; i++) {
        crc <<= 1;
        if ((crc & 0x10000) != 0) crc = crc ^ 0x1021;
    }
    const uint16_t c = static_cast<uint16_t>(crc);
    return static_cast<uint16_t>((c >> 8) | (c << 8));
}

} // namespace

uint16_t Crc16(const uint8_t* data, size_t size) {
    int32_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if ((crc & 0x10000) != 0) crc = (crc ^ 0x1021) & 0xFFFF;
        }
    }
    const uint16_t c = static_cast<uint16_t>(crc);
    return static_cast<uint16_t>((c >> 8) | (c << 8)); // stored byte-swapped, as the engine does
}

const std::vector<Field>& Fields() {
    // Word, shift and width from StoreDataBitFields (core_data.h); max from CoreData::IsValid (mii_types.h).
    static const std::vector<Field> f = {
        {"gender", "Gender (0 male, 1 female)", "Body", 1, 7, 1, 1},
        {"height", "Height", "Body", 0, 8, 7, 127},
        {"build", "Build", "Body", 0, 16, 7, 127},
        {"favorite_color", "Favourite colour", "Body", 5, 8, 4, 11},
        {"faceline_type", "Face shape", "Face", 5, 12, 4, 11},
        {"faceline_color", "Skin colour", "Face", 5, 16, 4, 9},
        {"faceline_wrinkle", "Wrinkles", "Face", 5, 20, 4, 11},
        {"faceline_makeup", "Makeup", "Face", 5, 24, 4, 11},
        {"hair_type", "Hair style", "Hair", 0, 0, 8, 131},
        {"hair_color", "Hair colour", "Hair", 0, 24, 7, 99},
        {"hair_flip", "Hair parted the other way", "Hair", 0, 23, 1, 1},
        {"eye_type", "Eyes", "Eyes", 2, 8, 6, 59},
        {"eye_color", "Eye colour", "Eyes", 1, 0, 7, 99},
        {"eye_scale", "Eye size", "Eyes", 4, 21, 3, 7},
        {"eye_aspect", "Eye stretch", "Eyes", 4, 13, 3, 6},
        {"eye_rotate", "Eye rotation", "Eyes", 4, 5, 3, 7},
        {"eye_x", "Eye spacing", "Eyes", 5, 28, 4, 12},
        {"eye_y", "Eye height", "Eyes", 2, 24, 5, 18},
        {"eyebrow_type", "Eyebrows", "Eyebrows", 3, 0, 5, 23},
        {"eyebrow_color", "Eyebrow colour", "Eyebrows", 1, 8, 7, 99},
        {"eyebrow_scale", "Eyebrow size", "Eyebrows", 6, 0, 4, 8},
        {"eyebrow_aspect", "Eyebrow stretch", "Eyebrows", 3, 29, 3, 6},
        {"eyebrow_rotate", "Eyebrow rotation", "Eyebrows", 6, 4, 4, 11},
        {"eyebrow_x", "Eyebrow spacing", "Eyebrows", 6, 8, 4, 12},
        {"eyebrow_y", "Eyebrow height", "Eyebrows", 6, 12, 4, 15},
        {"nose_type", "Nose", "Nose", 3, 8, 5, 17},
        {"nose_scale", "Nose size", "Nose", 6, 16, 4, 8},
        {"nose_y", "Nose height", "Nose", 3, 16, 5, 18},
        {"mouth_type", "Mouth", "Mouth", 2, 16, 6, 35},
        {"mouth_color", "Mouth colour", "Mouth", 1, 16, 7, 99},
        {"mouth_scale", "Mouth size", "Mouth", 6, 20, 4, 8},
        {"mouth_aspect", "Mouth stretch", "Mouth", 3, 21, 3, 6},
        {"mouth_y", "Mouth height", "Mouth", 3, 24, 5, 18},
        {"beard_type", "Beard", "Facial hair", 3, 13, 3, 5},
        {"beard_color", "Beard colour", "Facial hair", 1, 24, 7, 99},
        {"mustache_type", "Mustache", "Facial hair", 3, 5, 3, 5},
        {"mustache_scale", "Mustache size", "Facial hair", 6, 24, 4, 8},
        {"mustache_y", "Mustache height", "Facial hair", 4, 0, 5, 16},
        {"glasses_type", "Glasses", "Glasses", 5, 0, 5, 19},
        {"glasses_color", "Glasses colour", "Glasses", 2, 0, 7, 99},
        {"glasses_scale", "Glasses size", "Glasses", 2, 29, 3, 7},
        {"glasses_y", "Glasses height", "Glasses", 4, 8, 5, 20},
        {"mole_type", "Mole", "Mole", 0, 15, 1, 1},
        {"mole_scale", "Mole size", "Mole", 6, 28, 4, 8},
        {"mole_x", "Mole across", "Mole", 4, 16, 5, 16},
        {"mole_y", "Mole height", "Mole", 4, 24, 5, 30},
        // Not shown in the editor (kept as the game wrote them).
        {"type", "Special", "", 0, 31, 1, 1},
        {"region_move", "Region move", "", 2, 14, 2, 3},
        {"font_region", "Font region", "", 2, 22, 2, 3},
    };
    return f;
}

const Field* FindField(const char* key) {
    for (const Field& f : Fields())
        if (std::strcmp(f.key, key) == 0) return &f;
    return nullptr;
}

int StoreData::Get(const Field& f) const {
    return static_cast<int>((Word(raw, f.word) >> f.shift) & ((1u << f.bits) - 1));
}
void StoreData::Set(const Field& f, int value) {
    const uint32_t v = static_cast<uint32_t>(std::clamp(value, 0, f.max));
    const uint32_t mask = ((1u << f.bits) - 1) << f.shift;
    SetWord(raw, f.word, (Word(raw, f.word) & ~mask) | (v << f.shift));
}
int StoreData::Get(const char* key) const {
    const Field* f = FindField(key);
    return f ? Get(*f) : 0;
}
void StoreData::Set(const char* key, int value) {
    if (const Field* f = FindField(key)) Set(*f, value);
}

QString StoreData::Name() const {
    char16_t s[kNameLength + 1]{};
    std::memcpy(s, raw.data() + kNameOffset, 2 * kNameLength);
    return QString::fromUtf16(s);
}
void StoreData::SetName(const QString& name) {
    QString n = name.left(kNameLength);
    if (n.trimmed().isEmpty()) n = QStringLiteral("Mii");
    char16_t s[kNameLength]{};
    for (int i = 0; i < n.size() && i < kNameLength; i++) s[i] = n.at(i).unicode();
    std::memcpy(raw.data() + kNameOffset, s, 2 * kNameLength);
}
QByteArray StoreData::CreateId() const {
    return QByteArray(reinterpret_cast<const char*>(raw.data() + kCreateIdOffset), 16);
}
void StoreData::NewCreateId() {
    uint8_t id[16];
    for (auto& b : id) b = static_cast<uint8_t>(QRandomGenerator::global()->bounded(256));
    id[6] = (id[6] & 0x0F) | 0x40; // version 4
    id[8] = (id[8] & 0x3F) | 0x80; // RFC 4122 variant
    std::memcpy(raw.data() + kCreateIdOffset, id, 16);
}

void StoreData::UpdateChecksums() {
    PutCrc(raw.data() + kDataCrcOffset, Crc16(raw.data(), kCoreSize + 16));
    PutCrc(raw.data() + kDeviceCrcOffset, DeviceCrc16());
}

bool StoreData::IsValid(QString* why) const {
    const auto fail = [&](const QString& w) {
        if (why) *why = w;
        return false;
    };
    for (const Field& f : Fields())
        if (Get(f) > f.max) return fail(QStringLiteral("%1 is %2 (at most %3)").arg(QString::fromLatin1(f.key)).arg(Get(f)).arg(f.max));
    // The name: at least one character, then only zeros (Nickname::IsValid).
    char16_t s[kNameLength];
    std::memcpy(s, raw.data() + kNameOffset, sizeof(s));
    if (s[0] == 0) return fail(QStringLiteral("the name is empty"));
    int i = 1;
    while (i < kNameLength && s[i] != 0) i++;
    while (i < kNameLength && s[i] == 0) i++;
    if (i != kNameLength) return fail(QStringLiteral("the name has a gap"));
    uint16_t stored;
    std::memcpy(&stored, raw.data() + kDataCrcOffset, 2);
    if (stored != Crc16(raw.data(), kCoreSize + 16)) return fail(QStringLiteral("data checksum"));
    std::memcpy(&stored, raw.data() + kDeviceCrcOffset, 2);
    if (stored != DeviceCrc16()) return fail(QStringLiteral("device checksum"));
    return true;
}

StoreData StoreData::MakeDefault(const QString& name, bool female) {
    // StoreData::BuildBase with RawData::BaseMii[gender], colours already converted from their "Ver3" values.
    StoreData d;
    const std::pair<const char*, int> base[] = {
        {"faceline_type", 0}, {"faceline_color", 0}, {"faceline_wrinkle", 0}, {"faceline_makeup", 0},
        {"hair_type", female ? 12 : 33}, {"hair_color", 1}, {"hair_flip", 0},
        {"eye_type", female ? 4 : 2}, {"eye_color", 8}, {"eye_scale", 4}, {"eye_aspect", 3}, {"eye_rotate", female ? 3 : 4},
        {"eye_x", 2}, {"eye_y", 12},
        {"eyebrow_type", female ? 0 : 6}, {"eyebrow_color", 1}, {"eyebrow_scale", 4}, {"eyebrow_aspect", 3},
        {"eyebrow_rotate", 6}, {"eyebrow_x", 2}, {"eyebrow_y", 7},
        {"nose_type", 1}, {"nose_scale", 4}, {"nose_y", 9},
        {"mouth_type", 23}, {"mouth_color", 19}, {"mouth_scale", 4}, {"mouth_aspect", 3}, {"mouth_y", 13},
        {"mustache_type", 0}, {"beard_type", 0}, {"beard_color", 8}, {"mustache_scale", 4}, {"mustache_y", 10},
        {"glasses_type", 0}, {"glasses_color", 8}, {"glasses_scale", 4}, {"glasses_y", 10},
        {"mole_type", 0}, {"mole_scale", 4}, {"mole_x", 2}, {"mole_y", 20},
        {"height", 64}, {"build", 64}, {"gender", female ? 1 : 0}, {"favorite_color", 0},
        {"region_move", 0}, {"font_region", 0}, {"type", 0},
    };
    for (const auto& [k, v] : base) d.Set(k, v);
    d.SetName(name);
    d.NewCreateId();
    d.UpdateChecksums();
    return d;
}

Database::Database() = default;

std::optional<Database> Database::FromBytes(const QByteArray& bytes, QString* error) {
    const auto fail = [&](const QString& w) -> std::optional<Database> {
        if (error) *error = w;
        return std::nullopt;
    };
    if (bytes.size() != kDatabaseSize) return fail(QStringLiteral("wrong size (%1 bytes, expected %2)").arg(bytes.size()).arg(kDatabaseSize));
    const auto* p = reinterpret_cast<const uint8_t*>(bytes.constData());
    uint32_t magic;
    std::memcpy(&magic, p, 4);
    if (magic != kMagic) return fail(QStringLiteral("not a Mii database (no NFDB signature)"));
    const int tail = 4 + kMaxMiis * kStoreDataSize;
    const uint8_t version = p[tail], length = p[tail + 1];
    if (version != 1) return fail(QStringLiteral("unknown database version %1").arg(version));
    if (length >= kMaxMiis) return fail(QStringLiteral("bad Mii count %1").arg(length));
    uint16_t crc;
    std::memcpy(&crc, p + tail + 2, 2);
    if (crc != Crc16(p, kDatabaseSize - 2)) return fail(QStringLiteral("database checksum does not match"));
    Database db;
    for (int i = 0; i < length; i++) {
        StoreData d;
        std::memcpy(d.raw.data(), p + 4 + i * kStoreDataSize, kStoreDataSize);
        db.miis.push_back(d);
    }
    return db;
}

std::optional<Database> Database::Load(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1").arg(path);
        return std::nullopt;
    }
    return FromBytes(f.readAll(), error);
}

QByteArray Database::ToBytes() {
    QByteArray out(kDatabaseSize, '\0');
    auto* p = reinterpret_cast<uint8_t*>(out.data());
    std::memcpy(p, &kMagic, 4);
    for (size_t i = 0; i < miis.size(); i++) std::memcpy(p + 4 + i * kStoreDataSize, miis[i].raw.data(), kStoreDataSize);
    const int tail = 4 + kMaxMiis * kStoreDataSize;
    p[tail] = 1;
    p[tail + 1] = static_cast<uint8_t>(miis.size());
    const uint16_t crc = Crc16(p, kDatabaseSize - 2);
    std::memcpy(p + tail + 2, &crc, 2);
    return out;
}

bool Database::Save(const QString& path, QString* error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(ToBytes()) != kDatabaseSize || !f.commit()) {
        if (error) *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    return true;
}

void Database::Replace(int i, const StoreData& d) {
    if (i >= 0 && i < Count()) miis[static_cast<size_t>(i)] = d;
}
bool Database::Add(const StoreData& d) {
    if (Count() >= kMaxMiis - 1) return false; // the engine treats a full count (100) as broken
    miis.push_back(d);
    return true;
}
void Database::Remove(int i) {
    if (i >= 0 && i < Count()) miis.erase(miis.begin() + i);
}
void Database::Move(int from, int to) {
    if (from < 0 || from >= Count() || to < 0 || to >= Count() || from == to) return;
    const StoreData d = miis[static_cast<size_t>(from)];
    miis.erase(miis.begin() + from);
    miis.insert(miis.begin() + to, d);
}

} // namespace Mii
