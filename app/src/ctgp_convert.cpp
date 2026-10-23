// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctgp_convert.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

#include "zip_archive.h"

namespace CtgpConvert {
namespace {

quint16 U16(const QByteArray& d, int o) { return static_cast<quint16>(static_cast<uchar>(d[o]) | (static_cast<uchar>(d[o + 1]) << 8)); }
quint32 U32(const QByteArray& d, int o) {
    return static_cast<quint32>(static_cast<uchar>(d[o])) | (static_cast<quint32>(static_cast<uchar>(d[o + 1])) << 8) |
           (static_cast<quint32>(static_cast<uchar>(d[o + 2])) << 16) | (static_cast<quint32>(static_cast<uchar>(d[o + 3])) << 24);
}
void P16(QByteArray& d, quint16 v) { d.append(static_cast<char>(v & 0xFF)).append(static_cast<char>(v >> 8)); }
void P32(QByteArray& d, quint32 v) {
    for (int i = 0; i < 4; ++i) d.append(static_cast<char>((v >> (8 * i)) & 0xFF));
}

quint32 NameHash(const QString& name) {
    quint32 h = 0;
    for (const char c : name.toUtf8()) h = h * 0x65u + static_cast<quint8>(c);
    return h;
}

// ---------------------------------------------------------------------------------------------------------------- the game's tables

struct Course {
    const char* folder; ///< romfs Course/<folder>
    const char* music;  ///< Audio/Resource/Stream/pBGM_<music>_N / _F
    int label;          ///< Common.msbt name label (+100: the title form)
};
struct Cup {
    int label;          ///< Common.msbt cup-name label
    const char* icon;   ///< UI/cmn/race/ym_CupIcon<icon>_*.bntx
    Course courses[4];
};

// The game's 12 cups in menu order. Animal Crossing is one course with four seasonal folders: the track goes into all of them.
const Cup kCups[12] = {
    {1301, "Mushroom", {{"Gu_FirstCircuit", "WU_FIRST_CIRCUIT", 1401}, {"Gu_WaterPark", "WU_WATER_PARK", 1402}, {"Gu_Cake", "WU_CAKE", 1403}, {"Gu_DossunIseki", "WU_DOSSUN", 1404}}},
    {1302, "Flower", {{"Gu_MarioCircuit", "WU_CIRCUIT", 1406}, {"Gu_City", "WU_CITY", 1405}, {"Gu_HorrorHouse", "WU_HAUNTED", 1408}, {"Gu_Expert", "WU_EXPERT", 1411}}},
    {1303, "Star", {{"Gu_Airport", "WU_AIRPORT", 1409}, {"Gu_Ocean", "WU_OCEAN", 1410}, {"Gu_Techno", "WU_TECHNO", 1407}, {"Gu_SnowMountain", "WU_SNOW_MOUNTAIN", 1412}}},
    {1304, "Special", {{"Gu_Cloud", "WU_CLOUD", 1414}, {"Gu_Desert", "WU_DESERT", 1413}, {"Gu_BowserCastle", "WU_BOWSER_CASTLE", 1415}, {"Gu_RainbowRoad", "WU_RAINBOW_ROAD", 1416}}},
    {1305, "Shell", {{"Gwii_MooMooMeadows", "WII_MOO_MOO_MEADOWS", 1441}, {"Gagb_MarioCircuit", "AGB_MARIO_CIRCUIT", 1442}, {"Gds_PukupukuBeach", "DS_PUKUPUKU_BEACH", 1443}, {"G64_KinopioHighway", "N64_KINOPIO_HIGHWAY", 1445}}},
    {1306, "Banana", {{"Ggc_DryDryDesert", "GC_DRY_DRY_DESERT", 1447}, {"Gsfc_DonutsPlain3", "SFC_DONUTS_PLAIN3", 1446}, {"G64_PeachCircuit", "N64_PEACH_CIRCUIT", 1451}, {"G3ds_DKJungle", "3DS_DK_JUNGLE", 1448}}},
    {1307, "Leaf", {{"Gds_WarioStadium", "DS_WARIO_STADIUM", 1454}, {"Ggc_SherbetLand", "GC_SHERBET_LAND", 1449}, {"G3ds_MusicPark", "3DS_MUSIC_PARK", 1452}, {"G64_YoshiValley", "N64_YOSHI_VALLEY", 1453}}},
    {1308, "Thunder", {{"Gds_TickTockClock", "DS_TICK_TOCK_CLOCK", 1450}, {"G3ds_PackunSlider", "3DS_PACKUN_SLIDER", 1444}, {"Gwii_GrumbleVolcano", "WII_GRUMBLE_VOLCANO", 1455}, {"G64_RainbowRoad", "N64_RAINBOW_ROAD", 1456}}},
    {1309, "DLC02", {{"Dgc_YoshiCircuit", "GC_YOSHICIRCUIT", 1485}, {"Du_ExciteBike", "WU_EXCITEBIKE", 1482}, {"Du_DragonRoad", "WU_DRAGONROAD", 1483}, {"Du_MuteCity", "WU_MUTECITY", 1484}}},
    {1310, "DLC03", {{"Dwii_WariosMine", "WII_WARIOSMINE", 1481}, {"Dsfc_RainbowRoad", "SFC_RAINBOWROAD", 1486}, {"Du_IcePark", "WU_ICEPARK", 1487}, {"Du_Hyrule", "WU_HYRULE", 1488}}},
    {1311, "DLC04", {{"Dgc_BabyPark", "GC_BABYPARK", 1490}, {"Dagb_CheeseLand", "GBA_CHEESELAND", 1489}, {"Du_Woods", "WU_WOOD", 1491}, {"Du_Animal_Spring", "WU_ANIMALSPRING", 1492}}},
    {1312, "DLC05", {{"D3ds_NeoBowserCity", "3DS_NEOBOWSERCITY", 1493}, {"Dagb_RibbonRoad", "GBA_RIBBONROAD", 1494}, {"Du_Metro", "WU_METRO", 1495}, {"Du_BigBlue", "WU_BIGBLUE", 1496}}},
};

// CTGP DX's cups in the order of its cup-name labels 700000..700015, by cup-icon name.
const char* const kCtgpCupIcons[16] = {"Boo", "Blooper", "Penguin", "Packun", "CloudFlower", "SuperCrown", "GrandStar", "MultiMoon",
                                       "SuperLeaf", "Squid", "Heart", "ShineSprite", "ExtraMushroom", "IceFlower", "PranksterComet", "Coin"};
const char* const kIconVariants[4] = {"130x130^h", "Line128x128^s", "Line^s", "Nml^l"};
const char* const kLanguages[15] = {"CNzh", "EUde", "EUen", "EUes", "EUfr", "EUit", "EUnl", "EUpt", "EUru", "JPja", "KRko", "TWzh", "USen", "USes", "USfr"};

// CTGP DX's real cup layout (cup-major: cup k holds tracks 4k..4k+3, which are also the pack's CT_xx numbers). The pack's name labels are
// not in this order, so each track's name is looked up by its text.
const char* const kLayout[64] = {
    "Omatsuri Circuit", "Vanilla Lake 1", "Broken Pier", "Mystic Tangle", "Snow Panic", "Mario Circuit 2", "Volcanic Skyway", "Whale Lagoon",
    "DK Jungle", "Frappe Snowland", "Mario Circuit", "Jungle Falls", "Koopa Beach 1", "Mario Circuit", "Desert Hills", "Pill Hill Valley",
    "Lakeside Park", "Wario Circuit", "Banshee Boardwalk", "Creep Sea Cape", "Koopa Troopa Beach", "Wario Shipyard", "Somnom Labyrinth", "Walrus Cove",
    "Koopa Beach 2", "Green Hill Zone", "Cavi Cape Cliffside", "Nabbit's Secret Island", "Luigi Circuit", "Choco Island 1", "Boshi's Skatepark", "Windmill Isle",
    "Sky High Cathedral", "Shipwreck Shore", "Bowser's Castle", "DK's Jungle Parkway", "Humpback Pump Track", "Area 64", "Cheep Cheep Cove", "Teehee Trails",
    "Fossil Canyon", "DK Pass", "Bowser Castle 4", "Rainbow Road 1", "Dry Dry Falls", "Donut Plains 2", "Pac Mountain", "Vacay Bay",
    "Moo Moo Mountain", "Tropical Coast", "Shy Guy Bazaar", "Bowser's Castle", "Kingdom Way", "Alpine Skyway", "Bowser's Castle", "Rainbow Road",
    "Choco Island 1", "Haunted Woods", "Bowser's Castle", "Rainbow Road", "Mushroom Bridge", "Luigi Circuit", "Delfino Square", "Piranha Plant Pipeline",
};

QString NormName(const QString& text) {
    QString out;
    QString lower = text.toLower();
    lower.replace(QChar(0x2019), QLatin1Char('\''));
    for (const QChar ch : lower) {
        if (ch.isLetterOrNumber()) out.append(ch);
    }
    return out;
}

QString Utf16String(const QByteArray& bytes) {
    QString s = QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.constData()), bytes.size() / 2);
    while (s.endsWith(QChar(0))) s.chop(1);
    return s;
}

const QString kPackPrefix = QStringLiteral("atmosphere/contents/0100152000022000/romfs/");

} // namespace

// ------------------------------------------------------------------------------------------------------------------------------ SARC

bool Sarc::read(const QByteArray& d) {
    order.clear();
    files.clear();
    if (d.size() < 0x28 || d.mid(0, 4) != "SARC" || U16(d, 6) != 0xFEFF || d.mid(0x14, 4) != "SFAT") return false;
    const quint32 data_off = U32(d, 0x0C);
    const int count = U16(d, 0x1A);
    int pos = 0x20;
    struct Node { quint32 hash, attr, start, end; };
    QList<Node> nodes;
    for (int i = 0; i < count; ++i, pos += 16) nodes.append({U32(d, pos), U32(d, pos + 4), U32(d, pos + 8), U32(d, pos + 12)});
    if (d.mid(pos, 4) != "SFNT") return false;
    const int names_off = pos + 8;
    for (const auto& n : nodes) {
        QString name = QStringLiteral("%1").arg(n.hash, 8, 16, QLatin1Char('0'));
        if (n.attr >> 24 == 1) {
            const int noff = names_off + static_cast<int>(n.attr & 0xFFFFFF) * 4;
            const int nend = d.indexOf('\0', noff);
            if (nend < 0) return false;
            name = QString::fromUtf8(d.mid(noff, nend - noff));
        }
        files[name] = d.mid(static_cast<int>(data_off + n.start), static_cast<int>(n.end - n.start));
        order.append(name);
    }
    return true;
}

QByteArray Sarc::write() const {
    QList<QString> names = files.keys();
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) { return NameHash(a) < NameHash(b); });
    QByteArray sfnt, blob, nodes;
    QHash<QString, quint32> offsets;
    for (const auto& n : names) {
        offsets[n] = static_cast<quint32>(sfnt.size() / 4);
        sfnt.append(n.toUtf8()).append('\0');
        while (sfnt.size() % 4) sfnt.append('\0');
    }
    for (const auto& n : names) {
        while (blob.size() % 128) blob.append('\0'); // the game's message archives align every member to 0x80
        const quint32 start = static_cast<quint32>(blob.size());
        blob.append(files[n]);
        P32(nodes, NameHash(n));
        P32(nodes, 0x01000000u | offsets[n]);
        P32(nodes, start);
        P32(nodes, static_cast<quint32>(blob.size()));
    }
    QByteArray sfat("SFAT");
    P16(sfat, 0xC);
    P16(sfat, static_cast<quint16>(names.size()));
    P32(sfat, 0x65);
    sfat.append(nodes);
    QByteArray sfnt_blk("SFNT");
    P16(sfnt_blk, 8);
    P16(sfnt_blk, 0);
    sfnt_blk.append(sfnt);
    const int header_len = 0x14 + static_cast<int>(sfat.size() + sfnt_blk.size());
    const int data_off = (header_len + 0xFF) & ~0xFF; // the game's own message archives align their data to 0x100
    QByteArray out("SARC");
    P16(out, 0x14);
    P16(out, 0xFEFF);
    P32(out, static_cast<quint32>(data_off + blob.size()));
    P32(out, static_cast<quint32>(data_off));
    P16(out, 0x100);
    P16(out, 0);
    out.append(sfat).append(sfnt_blk);
    out.append(QByteArray(data_off - static_cast<int>(out.size()), '\0'));
    out.append(blob);
    return out;
}

// ------------------------------------------------------------------------------------------------------------------------------ MSBT

bool Msbt::read(const QByteArray& d) {
    sections.clear();
    labels.clear();
    strings.clear();
    groups.clear();
    lbl_dirty = false;
    if (d.size() < 0x20 || d.mid(0, 8) != "MsgStdBn" || d.mid(8, 2) != QByteArray("\xff\xfe", 2)) return false;
    bom_and_header = d.mid(8, 8);
    const int count = U16(d, 0x0E);
    int pos = 0x20;
    for (int i = 0; i < count; ++i) {
        if (pos + 16 > d.size()) return false;
        const QByteArray tag = d.mid(pos, 4);
        const int size = static_cast<int>(U32(d, pos + 4));
        if (pos + 16 + size > d.size()) return false;
        sections.append({tag, d.mid(pos + 16, size)});
        pos += 16 + ((size + 15) & ~15);
    }
    for (const auto& [tag, body] : sections) {
        if (tag == "LBL1") {
            const quint32 n = U32(body, 0);
            for (quint32 i = 0; i < n; ++i) {
                const quint32 cnt = U32(body, 4 + static_cast<int>(i) * 8);
                int p = static_cast<int>(U32(body, 8 + static_cast<int>(i) * 8));
                QList<QPair<QString, quint32>> group;
                for (quint32 k = 0; k < cnt; ++k) {
                    const int len = static_cast<uchar>(body[p]);
                    const QString name = QString::fromLatin1(body.mid(p + 1, len));
                    const quint32 idx = U32(body, p + 1 + len);
                    labels[name] = idx;
                    group.append({name, idx});
                    p += 1 + len + 4;
                }
                groups.append(group);
            }
        } else if (tag == "TXT2") {
            const int n = static_cast<int>(U32(body, 0));
            for (int i = 0; i < n; ++i) {
                const int o = static_cast<int>(U32(body, 4 + i * 4));
                const int e = i + 1 < n ? static_cast<int>(U32(body, 8 + i * 4)) : static_cast<int>(body.size());
                strings.append(body.mid(o, e - o));
            }
        }
    }
    return true;
}

void Msbt::add_label(const QString& name, const QString& text) {
    QByteArray raw;
    for (const QChar ch : text) P16(raw, ch.unicode());
    P16(raw, 0);
    add_label_raw(name, raw);
}

void Msbt::add_label_raw(const QString& name, const QByteArray& raw) {
    strings.append(raw);
    const quint32 idx = static_cast<quint32>(strings.size() - 1);
    quint32 h = 0;
    for (const char ch : name.toLatin1()) h = h * 0x492u + static_cast<quint8>(ch);
    groups[static_cast<int>(h % static_cast<quint32>(groups.size()))].append({name, idx});
    labels[name] = idx;
    lbl_dirty = true;
}

QByteArray Msbt::write() const {
    QByteArray blob;
    for (const auto& [tag, original] : sections) {
        QByteArray body = original;
        if (tag == "LBL1" && lbl_dirty) {
            body.clear();
            const int n = static_cast<int>(groups.size());
            P32(body, static_cast<quint32>(n));
            QByteArray entries;
            int pos = 4 + 8 * n;
            for (const auto& group : groups) {
                P32(body, static_cast<quint32>(group.size()));
                P32(body, static_cast<quint32>(pos + entries.size()));
                for (const auto& [name, idx] : group) {
                    const QByteArray raw = name.toLatin1();
                    entries.append(static_cast<char>(raw.size())).append(raw);
                    P32(entries, idx);
                }
            }
            body.append(entries);
        }
        if (tag == "TXT2") {
            body.clear();
            P32(body, static_cast<quint32>(strings.size()));
            int pos = 4 + 4 * static_cast<int>(strings.size());
            for (const auto& s : strings) {
                P32(body, static_cast<quint32>(pos));
                pos += static_cast<int>(s.size());
            }
            for (const auto& s : strings) body.append(s);
        }
        blob.append(tag);
        P32(blob, static_cast<quint32>(body.size()));
        blob.append(QByteArray(8, '\0'));
        blob.append(body);
        blob.append(QByteArray(((static_cast<int>(body.size()) + 15) & ~15) - static_cast<int>(body.size()), static_cast<char>(0xAB)));
    }
    QByteArray out("MsgStdBn");
    out.append(bom_and_header.left(2));
    out.append(QByteArray(2, '\0'));
    out.append(bom_and_header.mid(4, 2)); // encoding, version (bytes 0x0C, 0x0D)
    P16(out, static_cast<quint16>(sections.size()));
    P16(out, 0);
    P32(out, static_cast<quint32>(0x20 + blob.size()));
    out.append(QByteArray(10, '\0'));
    out.append(blob);
    return out;
}

// -------------------------------------------------------------------------------------------------------------------------- convert

bool Convert(const QString& zip_path, const QString& vanilla_ui_dir, const QString& out_dir, const Options& options, QString* error,
             const Progress& progress) {
    ZipArchive zip;
    if (!zip.open(zip_path, error)) return false;
    QHash<QString, const ZipEntry*> names;
    for (const auto& e : zip.entries()) {
        if (!e.is_directory() && e.name.startsWith(kPackPrefix)) names.insert(e.name.mid(kPackPrefix.size()), &e);
    }
    const auto fail = [&](const QString& text) {
        if (error) *error = text;
        return false;
    };
    static const QRegularExpression track_re(QStringLiteral("^(?:Course/CT_([0-9A-Fa-f]{2})/|Audio/Resource/Stream/pBGM_CT_([0-9A-Fa-f]{2})_|UI/cmn/menu/ym_CoursePict_CT_([0-9A-Fa-f]{2})_)"));
    QStringList wanted;
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        if (track_re.match(it.key()).hasMatch()) wanted << it.key();
    }
    if (wanted.isEmpty()) return fail(QStringLiteral("This zip has no CTGP DX tracks (Course/CT_xx)"));
    wanted.sort();
    int done = 0;
    const int total = static_cast<int>(wanted.size()) + 15;
    const auto step = [&](const QString& what) {
        ++done;
        return !progress || progress(done, total, what);
    };
    QDir(out_dir).removeRecursively();
    // 1. The tracks, unchanged, in their own folders.
    for (const auto& rel : wanted) {
        if (!zip.extract(*names.value(rel), QDir(out_dir).filePath(QStringLiteral("romfs/") + rel), error)) return false;
        if (!step(rel)) return fail(QStringLiteral("Cancelled"));
    }
    // 2. Track names as new labels in the game's own message files (every language the game has).
    const auto read_member = [&](const QString& member, Sarc& out) {
        const ZipEntry* e = names.value(member, nullptr);
        if (!e) return false;
        const QString tmp = QDir::temp().filePath(QStringLiteral("kartrecomp-message.sarc"));
        if (!zip.extract(*e, tmp, error)) return false;
        QFile f(tmp);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const bool ok = out.read(f.readAll());
        f.close();
        QFile::remove(tmp);
        return ok;
    };
    const auto text_of = [](const Msbt& m, const QString& label) {
        const auto it = m.labels.constFind(label);
        return it == m.labels.constEnd() ? QString{} : Utf16String(m.strings[static_cast<int>(it.value())]);
    };
    Sarc pack_en;
    Msbt en;
    if (!read_member(QStringLiteral("UI/USen/message.sarc"), pack_en) || !en.read(pack_en.files.value(QStringLiteral("CTGPText.msbt")))) return fail(QStringLiteral("The pack's English message file is unreadable"));
    QHash<QString, QString> en_by_name; // normalised English name -> label 5001xx
    {
        QList<int> labs;
        for (auto it = en.labels.constBegin(); it != en.labels.constEnd(); ++it) {
            bool ok = false;
            const int n = it.key().toInt(&ok);
            if (ok && n >= 500100 && n <= 500199) labs << n;
        }
        std::sort(labs.begin(), labs.end());
        for (const int n : labs) {
            const QString key = NormName(text_of(en, QString::number(n)));
            if (!en_by_name.contains(key)) en_by_name.insert(key, QString::number(n));
        }
    }
    Sarc van_en_sarc;
    Msbt van_en;
    {
        QFile f(QDir(vanilla_ui_dir).filePath(QStringLiteral("UI/USen/message.sarc")));
        if (!f.open(QIODevice::ReadOnly) || !van_en_sarc.read(f.readAll()) || !van_en.read(van_en_sarc.files.value(QStringLiteral("Common.msbt")))) return fail(QStringLiteral("Cannot read the game's English message file"));
    }
    QHash<QString, QString> game_by_name; // the few tracks that keep the game's own names
    for (int n = 1401; n <= 1496; ++n) {
        const QString label = QString::number(n);
        if (van_en.labels.contains(label)) {
            const QString key = NormName(text_of(van_en, label));
            if (!game_by_name.contains(key)) game_by_name.insert(key, label);
        }
    }
    for (const char* lang_c : kLanguages) {
        const QString lang = QLatin1String(lang_c);
        QFile van_file(QDir(vanilla_ui_dir).filePath(QStringLiteral("UI/%1/message.sarc").arg(lang)));
        if (!van_file.open(QIODevice::ReadOnly)) {
            step(lang);
            continue;
        }
        Sarc van;
        if (!van.read(van_file.readAll())) return fail(QStringLiteral("Cannot read the game's message file for %1").arg(lang));
        Sarc pack;
        if (!read_member(names.contains(QStringLiteral("UI/%1/message.sarc").arg(lang)) ? QStringLiteral("UI/%1/message.sarc").arg(lang) : QStringLiteral("UI/USen/message.sarc"), pack) ||
            !pack.files.contains(QStringLiteral("CTGPText.msbt"))) return fail(QStringLiteral("The pack's message file for %1 is unreadable").arg(lang));
        Msbt common, ctext;
        if (!common.read(van.files.value(QStringLiteral("Common.msbt"))) || !ctext.read(pack.files.value(QStringLiteral("CTGPText.msbt")))) return fail(QStringLiteral("Cannot read message tables for %1").arg(lang));
        for (int j = 0; j < 64; ++j) {
            const QString key = NormName(QString::fromLatin1(kLayout[j]));
            QString text, tagged;
            if (en_by_name.contains(key)) {
                const QString label = en_by_name.value(key);
                text = text_of(ctext, label);
                const QString tag_label = QString::number(label.toInt() + 100);
                tagged = ctext.labels.contains(tag_label) ? text_of(ctext, tag_label) : text;
            } else if (game_by_name.contains(key)) {
                const QString label = game_by_name.value(key);
                text = text_of(common, label);
                const QString tag_label = QString::number(label.toInt() + 100);
                tagged = common.labels.contains(tag_label) ? text_of(common, tag_label) : text;
            } else {
                continue;
            }
            common.add_label(QString::number(21000 + j), text);
            common.add_label(QString::number(21100 + j), tagged);
        }
        van.files[QStringLiteral("Common.msbt")] = common.write();
        const QString target = QDir(out_dir).filePath(QStringLiteral("romfs/UI/%1/message.sarc").arg(lang));
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(van.write()) < 0) return fail(QStringLiteral("Cannot write the message files"));
        if (!step(lang)) return fail(QStringLiteral("Cancelled"));
    }
    // 3. The cup list for the in-game "Extra cup" menu: CTGP DX's 16 cups of four tracks (cup k = tracks 4k..4k+3; names from label 700000+k).
    if (!options.cups_list_path.isEmpty()) {
        QFile list(options.cups_list_path);
        if (!list.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail(QStringLiteral("Cannot write the cup list"));
        for (int k = 0; k < 16; ++k) {
            QString name = text_of(en, QString::number(700000 + k));
            if (name.isEmpty()) name = QStringLiteral("Cup %1").arg(k + 1);
            QStringList tracks;
            for (int r = 0; r < 4; ++r) tracks << QStringLiteral("CT_%1").arg(k * 4 + r, 2, 16, QLatin1Char('0')).toUpper().replace(QStringLiteral("CT_"), QStringLiteral("CT_"));
            list.write((name.toLatin1().replace('|', ' ') + "|" + tracks.join(QLatin1Char(' ')).toLatin1() + "\n"));
        }
    }
    return true;
}

} // namespace CtgpConvert
