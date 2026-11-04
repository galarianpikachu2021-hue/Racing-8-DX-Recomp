// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "keys_check.h"
#include "nsp_info.h"
#include "setup_validate.h"

namespace {
void Write(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
}
const QByteArray kGoodKeys = "header_key = 00\naes_kek_generation_source = 00\nkey_area_key_application_source = 00\n"
                             "aes_key_generation_source = 00\ntitlekek_source = 00\nmaster_key_00 = 00\nmaster_key_0b = 00\n";
} // namespace

class TestSetupValidate : public QObject {
    Q_OBJECT
private slots:
    void parsesTheExporterInfoLine() {
        const auto i = ParseNspInfo(R"({"titleId":"0100152000022800","version":1310720,"aarch64":true,"hasProgram":true,"error":""})");
        QCOMPARE(i.titleId, quint64(0x0100152000022800ULL));
        QCOMPARE(i.version, quint32(1310720));
        QVERIFY(i.aarch64 && i.hasProgram);
        QVERIFY(i.error.isEmpty());
    }
    void garbageFromTheExporterBecomesAnError() {
        QVERIFY(!ParseNspInfo("segfault").error.isEmpty());
        QVERIFY(!ParseNspInfo("").error.isEmpty());
        QCOMPARE(ParseNspInfo(R"({"titleId":"","version":0,"aarch64":false,"hasProgram":false,"error":"not an NSP"})").error, QString("not an NSP"));
    }
    void aMissingHeaderKeyIsNamed() {
        QTemporaryDir d;
        Write(d.filePath("keys/prod.keys"), "master_key_00 = 00\n");
        QCOMPARE(CheckKeys(d.filePath("keys/prod.keys")), KeyProblem::MissingHeaderKey);
        QVERIFY(KeyProblemText(KeyProblem::MissingHeaderKey).contains("header_key"));
        QCOMPARE(CheckKeys(d.filePath("nope.keys")), KeyProblem::NoFile);
    }
    void keysWithoutAnyMasterKeyAreRefused() {
        QTemporaryDir d;
        Write(d.filePath("prod.keys"), "header_key = 00\naes_kek_generation_source = 00\nkey_area_key_application_source = 00\n"
                                       "aes_key_generation_source = 00\ntitlekek_source = 00\n");
        QCOMPARE(CheckKeys(d.filePath("prod.keys")), KeyProblem::MissingMasterKey);
    }
    void goodKeysInAPathWithSpacesAndNonAsciiPass() {
        QTemporaryDir d;
        const QString p = d.filePath(QString::fromUtf8("meine Schlüssel/prod.keys"));
        Write(p, kGoodKeys);
        QCOMPARE(CheckKeys(p), KeyProblem::None);
    }
    void theRightUpdateAndBasePass() {
        QTemporaryDir d;
        Write(d.filePath("prod.keys"), kGoodKeys);
        Write(d.filePath("base.nsp"), "x");
        Write(d.filePath("update.nsp"), "x");
        QDir().mkpath(d.filePath("fw"));
        Write(d.filePath("fw/a.nca"), "x");
        SetupInputs in{d.filePath("base.nsp"), d.filePath("update.nsp"), d.filePath("prod.keys"), d.filePath("fw"), {}};
        const auto reader = [](const QString& path) {
            NspInfo i;
            i.hasProgram = true;
            if (path.endsWith("base.nsp")) { i.titleId = 0x0100152000022000ULL; i.version = 0; i.aarch64 = true; }
            else { i.titleId = 0x0100152000022800ULL; i.version = 20u << 16; i.aarch64 = true; }
            return i;
        };
        QCOMPARE(ValidateInputs(in, reader), QString());
    }
    void anOlder32BitUpdateIsRefusedWithTheReason() {
        NspInfo i;
        i.titleId = 0x0100152000022800ULL;
        i.version = 19u << 16;
        i.aarch64 = false;
        i.hasProgram = true;
        const QString why = ValidateUpdate(i);
        QVERIFY2(why.contains("32-bit"), qPrintable(why));
    }
    void aNewerUpdateIsRefusedAsUnsupported() {
        NspInfo i;
        i.titleId = 0x0100152000022800ULL;
        i.version = 23u << 16;
        i.aarch64 = true;
        i.hasProgram = true;
        QVERIFY(ValidateUpdate(i).contains("3.0.4"));
    }
    void aBaseNspFromAnotherGameNamesItsTitleId() {
        NspInfo i;
        i.titleId = 0x01007EF00011E000ULL;
        i.hasProgram = true;
        const QString why = ValidateBase(i);
        QVERIFY2(why.contains("01007ef00011e000"), qPrintable(why));
    }
    void anNspThatCannotBeReadPassesTheExportersReasonOn() {
        NspInfo i;
        i.error = "the keys cannot open it";
        QVERIFY(ValidateBase(i).contains("the keys cannot open it"));
        QVERIFY(ValidateUpdate(i).contains("the keys cannot open it"));
    }
    void firmwareMustBeAFolderWithNcaFilesOrAZip() {
        QTemporaryDir d;
        QDir().mkpath(d.filePath("empty"));
        QDir().mkpath(d.filePath("fw"));
        Write(d.filePath("fw/x.nca"), "x");
        Write(d.filePath("fw.zip"), "PK");
        QVERIFY(!ValidateFirmware(d.filePath("empty")).isEmpty());
        QVERIFY(!ValidateFirmware(d.filePath("missing")).isEmpty());
        QCOMPARE(ValidateFirmware(d.filePath("fw")), QString());
        QCOMPARE(ValidateFirmware(d.filePath("fw.zip")), QString());
    }
    void theRealFilesPassThroughTheRealExporter() {
        // KARTRECOMP_EXPORT_BIN=<kartrecomp-export>; repo root as the working directory; skipped without it
        const QString exporter = qEnvironmentVariable("KARTRECOMP_EXPORT_BIN");
        if (exporter.isEmpty()) QSKIP("no exporter given");
        const QString keys = QDir::current().filePath("local/suyu-home/suyu/keys/prod.keys");
        SetupInputs in{QDir::current().filePath("local/game/content/base.nsp"), QDir::current().filePath("local/game/content/update-3.0.4.nsp"),
                       keys, QDir::current().filePath("local/suyu-home/suyu/nand/system/Contents/registered"), {}};
        QCOMPARE(CheckKeys(keys), KeyProblem::None);
        const auto reader = [&](const QString& p) { return ReadNspInfo(exporter, p, keys); };
        QVERIFY2(ValidateBase(reader(in.base)).isEmpty(), qPrintable(ValidateBase(reader(in.base))));
        QVERIFY2(ValidateUpdate(reader(in.update)).isEmpty(), qPrintable(ValidateUpdate(reader(in.update))));
        const QString old = qEnvironmentVariable("KARTRECOMP_OLD_UPDATE");
        if (!old.isEmpty()) QVERIFY(ValidateUpdate(reader(old)).contains("32-bit"));
    }
    void dlcMustBeNspFiles() {
        QTemporaryDir d;
        Write(d.filePath("dlc.txt"), "x");
        QVERIFY(!ValidateDlc({d.filePath("dlc.txt")}).isEmpty());
        Write(d.filePath("ok.nsp"), "x");
        QCOMPARE(ValidateDlc({d.filePath("ok.nsp")}), QString());
        QCOMPARE(ValidateDlc({}), QString());
    }
};
QTEST_APPLESS_MAIN(TestSetupValidate)
#include "test_setup_validate.moc"
