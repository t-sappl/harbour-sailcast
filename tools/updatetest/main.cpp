// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests of the pure functions of UpdateChecker (350)
#include <QCoreApplication>
#include <QDebug>
#include "updatechecker.h"
#include "releasenotes.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; qWarning("FAIL line %d: %s", __LINE__, #cond); } } while (0)

static QByteArray release(const char *tag, bool draft, bool pre, const char *assets)
{
    return QByteArray("{\"tag_name\":\"") + tag + "\",\"draft\":" + (draft ? "true" : "false")
            + ",\"prerelease\":" + (pre ? "true" : "false") + ",\"body\":\"## New\\n- Chapters\\n**bold** [link](http://x)\\n\\nDeveloped by Thomas Sappl together with Claude\","
            + "\"assets\":[" + assets + "]}";
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // versions
    CHECK(UpdateChecker::parseVersion("v1.5.0") == (QList<int>() << 1 << 5 << 0));
    CHECK(UpdateChecker::parseVersion("1.5") == (QList<int>() << 1 << 5 << 0));
    CHECK(UpdateChecker::parseVersion("1.4.1 (build 345)") == (QList<int>() << 1 << 4 << 1));
    CHECK(UpdateChecker::parseVersion("nightly").isEmpty());
    CHECK(UpdateChecker::compareVersions("v1.5.0", "1.4.1 (build 345)") > 0);
    CHECK(UpdateChecker::compareVersions("1.4.1", "1.4.1 (build 346)") == 0);
    CHECK(UpdateChecker::compareVersions("1.10.0", "1.9.9") > 0);
    CHECK(UpdateChecker::compareVersions("v1.4", "1.4.0") == 0);
    CHECK(UpdateChecker::compareVersions("junk", "1.4.0") == 0);
    // architecture
    CHECK(UpdateChecker::architectureFor("arm64") == "aarch64");
    CHECK(UpdateChecker::architectureFor("arm") == "armv7hl");
    CHECK(UpdateChecker::architectureFor("i386") == "i486");
    CHECK(UpdateChecker::architectureFor("mips").isEmpty());
    // release parsing
    const char *both =
        "{\"name\":\"harbour-sailcast-1.5.0-1.aarch64.rpm\",\"size\":2200000,\"browser_download_url\":\"https://github.com/t-sappl/harbour-sailcast/releases/download/v1.5.0/harbour-sailcast-1.5.0-1.aarch64.rpm\"},"
        "{\"name\":\"harbour-sailcast-1.5.0-1.armv7hl.rpm\",\"size\":2100000,\"browser_download_url\":\"https://github.com/t-sappl/harbour-sailcast/releases/download/v1.5.0/harbour-sailcast-1.5.0-1.armv7hl.rpm\"}";
    UpdateInfo a = UpdateChecker::parseRelease(release("v1.5.0", false, false, both), "armv7hl");
    CHECK(a.valid && a.version == "1.5.0" && a.assetName == "harbour-sailcast-1.5.0-1.armv7hl.rpm" && a.assetSize == 2100000);
    CHECK(a.notes == QString::fromUtf8("New\n\xE2\x80\xA2 Chapters bold link"));
    CHECK(!UpdateChecker::parseRelease(release("v1.5.0", false, false, both), "i486").valid);   // no asset for the architecture
    CHECK(!UpdateChecker::parseRelease(release("v1.5.0", true, false, both), "armv7hl").valid); // draft
    CHECK(!UpdateChecker::parseRelease(release("v1.5.0", false, true, both), "armv7hl").valid); // pre-release
    CHECK(!UpdateChecker::parseRelease(release("latest", false, false, both), "armv7hl").valid);
    CHECK(!UpdateChecker::parseRelease("not json", "armv7hl").valid);
    CHECK(!UpdateChecker::parseRelease("{}", "armv7hl").valid);
    const char *foreign =
        "{\"name\":\"harbour-sailcast-1.5.0-1.aarch64.rpm\",\"size\":1,\"browser_download_url\":\"https://evil.example/harbour-sailcast-1.5.0-1.aarch64.rpm\"}";
    CHECK(!UpdateChecker::parseRelease(release("v1.5.0", false, false, foreign), "aarch64").valid);
    const char *other =
        "{\"name\":\"something-else.rpm\",\"size\":1,\"browser_download_url\":\"https://github.com/t-sappl/harbour-sailcast/releases/download/v1.5.0/something-else.rpm\"}";
    CHECK(!UpdateChecker::parseRelease(release("v1.5.0", false, false, other), "aarch64").valid);
    // the asset must be the tagged version (old spec version in a new release)
    const char *stale =
        "{\"name\":\"harbour-sailcast-1.4.1-1.aarch64.rpm\",\"size\":1,\"browser_download_url\":\"https://github.com/t-sappl/harbour-sailcast/releases/download/v1.5.0/harbour-sailcast-1.4.1-1.aarch64.rpm\"}";
    CHECK(!UpdateChecker::parseRelease(release("v1.5.0", false, false, stale), "aarch64").valid);
    CHECK(UpdateChecker::parseRelease(release("v1.5", false, false, both), "aarch64").valid);
    // package names and clean-up
    CHECK(UpdateChecker::isOwnPackage("harbour-sailcast-1.5.0-1.aarch64.rpm"));
    CHECK(UpdateChecker::isOwnPackage("harbour-sailcast-1.5-2.i486.rpm.part"));
    CHECK(!UpdateChecker::isOwnPackage("harbour-sailwave-1.5.0-1.aarch64.rpm"));
    CHECK(!UpdateChecker::isOwnPackage("../harbour-sailcast-1.5.0-1.aarch64.rpm"));
    CHECK(!UpdateChecker::isOwnPackage("harbour-sailcast-notes.txt"));
    QStringList names;
    names << "harbour-sailcast-1.4.0-1.aarch64.rpm" << "harbour-sailcast-1.4.1-1.aarch64.rpm"
          << "harbour-sailcast-1.5.0-1.aarch64.rpm" << "harbour-sailcast-1.6.0-1.aarch64.rpm.part"
          << "holiday.rpm" << "sailcast-subscriptions.opml";
    const QStringList gone = UpdateChecker::obsoletePackages(names, "1.5.0 (build 350)");
    CHECK(gone.size() == 4);
    CHECK(gone.contains("harbour-sailcast-1.5.0-1.aarch64.rpm"));
    CHECK(gone.contains("harbour-sailcast-1.6.0-1.aarch64.rpm.part"));
    const QStringList keep = UpdateChecker::obsoletePackages(
        QStringList() << "harbour-sailcast-1.6.0-1.aarch64.rpm", "1.5.0");
    CHECK(keep.isEmpty());    // a newer package already downloaded stays
    // (347) release notes and changelog
    const QString b = QString::fromUtf8("\xE2\x80\xA2 ");
    CHECK(ReleaseNotes::plain("Intro line.\n\n### New\n- **A** thing\n  wrapped here\n- B\n\n### Install\nAllow untrusted software\n- `aarch64`\n\n---\n\nDeveloped by Thomas")
          == "Intro line.\n\nNew\n" + b + "A thing wrapped here\n" + b + "B");
    CHECK(ReleaseNotes::plain("### Install\nx\n### Fixed\n- y") == "Fixed\n" + b + "y");
    CHECK(ReleaseNotes::plain("### Installation notes\nkept") == "Installation notes\nkept");
    CHECK(ReleaseNotes::plain("- Tabs:\n  - For you\n  - New") == b + "Tabs:\n   " + QString::fromUtf8("\xE2\x97\xA6 ") + "For you\n   " + QString::fromUtf8("\xE2\x97\xA6 ") + "New");
    CHECK(ReleaseNotes::plain("1. a\n2. b\n   more") == "1. a\n2. b more");
    // (365) German block of the GitHub release text
    {
        const QString body = "### New\n- A\n\n---\n\nDeveloped by Thomas\n\n<details>\n<summary>Deutsch</summary>\n\n### Neu\n- A de\n\n</details>\n";
        CHECK(ReleaseNotes::plain(ReleaseNotes::localized(body, false)) == "New\n" + b + "A");
        CHECK(ReleaseNotes::plain(ReleaseNotes::localized(body, true)) == "Neu\n" + b + "A de");
        CHECK(ReleaseNotes::plain(ReleaseNotes::localized("### New\n- A", true)) == "New\n" + b + "A");
        CHECK(ReleaseNotes::plain(ReleaseNotes::localized("<details><summary>Deutsch</summary>\n- x\n</details>\n- y", false)) == b + "y");
        CHECK(ReleaseNotes::plain(ReleaseNotes::localized("<details>\n<summary>More</summary>\nkept\n</details>", false)).contains("kept"));
    }
    // (351) The blocks of the About page
    {
        const QList<ReleaseNotes::Block> bl = ReleaseNotes::blocks("Intro\n\n### New\n- A\n  wrapped\n  - sub\n1. one\n\n### Install\nx\n");
        CHECK(bl.size() == 6);
        CHECK(bl.size() == 6 && bl.at(0).kind == ReleaseNotes::Block::Paragraph && bl.at(0).text == "Intro"
              && bl.at(1).kind == ReleaseNotes::Block::Blank
              && bl.at(2).kind == ReleaseNotes::Block::Heading && bl.at(2).text == "New"
              && bl.at(3).kind == ReleaseNotes::Block::Item && bl.at(3).text == "A wrapped"
              && bl.at(4).kind == ReleaseNotes::Block::SubItem && bl.at(4).text == "sub"
              && bl.at(5).kind == ReleaseNotes::Block::Numbered && bl.at(5).text == "1. one");
        CHECK(ReleaseNotes::blocks("### A\ntext\nmore").size() == 2
              && ReleaseNotes::blocks("### A\ntext\nmore").at(1).text == "text more");
    }
    const QList<ReleaseNotes::Section> secs = ReleaseNotes::sections(
        "# Changelog\n\n## 1.4.1 (2026-11-17)\n\nSmall.\n\n### New\n- X\n\n## 1.4 (2026-11-15)\n- Y\n  more\n## 1.0\n- Z\n");
    CHECK(secs.size() == 3);
    CHECK(secs.size() == 3 && secs.at(0).version == "1.4.1" && secs.at(0).date == "2026-11-17"
          && secs.at(0).text == "Small.\n\nNew\n" + b + "X");
    CHECK(secs.size() == 3 && secs.at(0).blocks.size() == 4);
    CHECK(secs.size() == 3 && secs.at(1).text == b + "Y more" && secs.at(2).version == "1.0" && secs.at(2).date.isEmpty());
    if (failures == 0) {
        qInfo("all update tests passed");
    }
    return failures == 0 ? 0 : 1;
}
