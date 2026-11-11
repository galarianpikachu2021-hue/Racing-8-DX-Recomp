// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "test_support.h"

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
    void anExporterThatCrashesIsReportedAsWrongKeysOrADamagedFile() {
        NEEDS_SH();
        QTemporaryDir d;
        const QString script = d.filePath("crashing-exporter.sh");
        Write(script, "#!/bin/sh\nkill -SEGV $$\n");
        QFile::setPermissions(script, QFile::permissions(script) | QFileDevice::ExeOwner);
        const NspInfo info = ReadNspInfo(script, d.filePath("x.nsp"), d.filePath("prod.keys"));
        QVERIFY(!info.error.isEmpty());
        QVERIFY2(info.error.contains("keys") && info.error.contains("damaged"), qPrintable(info.error));
    }
    void garbageFromTheExporterBecomesAnError() {
        QVERIFY(!ParseNspInfo("segfault").error.isEmpty());
        QVERIFY(!ParseNspInfo("").error.isEmpty());
        QVERIFY(FirmwareTooNew("/x/Firmware.23.0.1").contains("23.0.1"));
        QVERIFY(FirmwareTooNew("C:/dl/Firmware 22.6.0.zip").contains("22.6.0"));
        QVERIFY(FirmwareTooNew("/x/Firmware.22.5.0").isEmpty());
        QVERIFY(FirmwareTooNew("/x/Firmware.20.5.0.zip").isEmpty());
        QVERIFY(FirmwareTooNew("/x/firmware").isEmpty());
        // log or key-file warnings around the answer (newer prod.keys) must not hide it
        const auto noisy = ParseNspInfo("[ 0.01] Crypto <Warning> unknown key 'xyz'\n{\"titleId\":\"0100152000022000\",\"version\":0,\"aarch64\":true,\"hasProgram\":true,\"error\":\"\"}\n");
        QVERIFY(noisy.error.isEmpty());
        QCOMPARE(noisy.titleId, 0x0100152000022000ULL);
        QVERIFY(ParseNspInfo("only a warning\n").error.contains("only a warning"));
        QCOMPARE(ParseNspInfo(R"({"titleId":"","version":0,"aarch64":false,"hasProgram":false,"error":"not an NSP"})").error, QString("not an NSP"));
    }
    void aMissingHeaderKeyIsNamed() {
        QTemporaryDir d;
        Write(d.filePath("keys/prod.keys"), "master_key_00 = 00\n");
        QCOMPARE(CheckKeys(d.filePath("keys/prod.keys")), KeyProblem::MissingHeaderKey);
        QVERIFY(KeyProblemText(KeyProblem::MissingHeaderKey).contains("header_key"));
        QCOMPARE(CheckKeys(d.filePath("nope.keys")), KeyProblem::NoFile);
    }
    void aMissingDerivationKeyIsNamedExactly() {
        QTemporaryDir d;
        Write(d.filePath("prod.keys"), "header_key = 00\naes_kek_generation_source = 00\nkey_area_key_application_source = 00\n"
                                       "aes_key_generation_source = 00\nmaster_key_00 = 00\n"); // no titlekek_source
        QCOMPARE(CheckKeys(d.filePath("prod.keys")), KeyProblem::MissingDerivationKey);
        QCOMPARE(MissingKeyName(d.filePath("prod.keys")), QString("titlekek_source"));
        QVERIFY(KeyProblemText(KeyProblem::MissingDerivationKey, "titlekek_source").contains("titlekek_source"));
        QVERIFY(!KeyProblemText(KeyProblem::MissingDerivationKey, "titlekek_source").contains("aes_kek_generation_source")); // only the missing one
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
    void update401IsAccepted() {
        NspInfo i;
        i.titleId = 0x0100152000022800ULL;
        i.version = 23u << 16; // 4.0.1
        i.aarch64 = true;
        i.hasProgram = true;
        QVERIFY2(ValidateUpdate(i).isEmpty(), qPrintable(ValidateUpdate(i)));
        QCOMPARE(UpdateName(23u << 16), QString("4.0.1"));
        QCOMPARE(UpdateName(20u << 16), QString("3.0.4"));
    }
    void anUnknownUpdateIsRefusedNamingTheSupportedOnes() {
        NspInfo i;
        i.titleId = 0x0100152000022800ULL;
        i.version = 24u << 16;
        i.aarch64 = true;
        i.hasProgram = true;
        const QString why = ValidateUpdate(i);
        QVERIFY2(why.contains("3.0.4") && why.contains("4.0.1"), qPrintable(why));
    }
    void hotListFollowsTheUpdate() {
        QCOMPARE(HotListFor("/x/data/mk8d-3.0.4/hot_blocks.txt", "4.0.1"), QString("/x/data/mk8d-4.0.1/hot_blocks.txt"));
        QCOMPARE(HotListFor("/x/data/mk8d-3.0.4/hot_blocks.txt", "3.0.4"), QString("/x/data/mk8d-3.0.4/hot_blocks.txt"));
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
