#include "skin/DefaultSkin.h"

#include <QImage>
#include <QPainter>
#include <QTest>

using namespace newamp;
using sprites::Sheet;

class TestSkin : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaultSkinHasEverySheetAtClassicSize()
    {
        const Skin skin = DefaultSkin::create();
        for (int i = 0; i < int(Sheet::Count); ++i) {
            const Sheet sheet = Sheet(i);
            QVERIFY2(!skin.sheet(sheet).isNull(), qPrintable(QString::number(i)));
            QCOMPARE(skin.sheet(sheet).size(), sprites::sheetSize(sheet));
        }
    }

    void glyphLookup()
    {
        QCOMPARE(Skin::glyphCell(QLatin1Char('A')), QPoint(0, 0));
        QCOMPARE(Skin::glyphCell(QLatin1Char('a')), QPoint(0, 0));
        QCOMPARE(Skin::glyphCell(QLatin1Char('Z')), QPoint(25, 0));
        QCOMPARE(Skin::glyphCell(QLatin1Char('0')), QPoint(0, 1));
        QCOMPARE(Skin::glyphCell(QLatin1Char(':')), QPoint(12, 1));
        QCOMPARE(Skin::glyphCell(QLatin1Char(' ')), QPoint(30, 0));
        QCOMPARE(Skin::glyphCell(QChar(0x263A)), QPoint(30, 0)); // unknown -> space
    }

    void textIsDrawnFromTheFontSheet()
    {
        const Skin skin = DefaultSkin::create();
        QImage image(20, 6, QImage::Format_ARGB32);
        image.fill(Qt::black);
        {
            QPainter p(&image);
            skin.drawText(p, {0, 0}, QStringLiteral("A"));
        }
        // 'A' starts with a blank pixel then a lit one on its top row.
        const QImage font = skin.sheet(Sheet::Text).toImage();
        QCOMPARE(image.pixel(1, 0), font.pixel(1, 0));
        QVERIFY(image.pixel(1, 0) != font.pixel(0, 0));
    }

    void volumeBackgroundsFitTheSheet()
    {
        const QRect sheet(QPoint(0, 0), sprites::sheetSize(Sheet::Volume));
        for (int step = 0; step < sprites::VolumeSteps; ++step)
            QVERIFY(sheet.contains(sprites::volumeBackground(step)));
        QVERIFY(sheet.contains(sprites::VolumeThumb.normal));
    }
};

QTEST_MAIN(TestSkin)
#include "tst_skin.moc"
