// label_utils.cpp
#include "label_utils.h"
#include <QDir>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QStringList>

// Klasörde .txt/.xml var mı?
static bool hasLabels(const QString& dirPath)
{
    QDir d(dirPath);
    const int nTxt = d.entryList({"*.txt"}, QDir::Files | QDir::Readable).size();
    const int nXml = d.entryList({"*.xml"}, QDir::Files | QDir::Readable).size();
    return (nTxt + nXml) > 0;
}

QString autoFindOtherLabels(const QString& oursTrainDir)
{
    // Beklenen giriş: .../labels_XXX/train
    QFileInfo inFi(oursTrainDir);
    if (!inFi.exists() || !inFi.isDir()) return {};

    QDir trainDir(oursTrainDir);
    if (trainDir.dirName().compare("train", Qt::CaseInsensitive) != 0) return {};

    // .../labels_XXX/train  -> labels_XXX
    QDir labelsDir = trainDir; labelsDir.cdUp();
    const QString ourBase = labelsDir.dirName(); // örn: labels_yolo, labels_voc

    // .../labels_XXX        -> dataset kökü
    QDir root = labelsDir; root.cdUp();

    // Kök altındaki alt dizinler
    const QStringList subdirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    // LabelImg için tipik ekler (öncelik sırası)
    const QStringList preferKeys = {"_li", "_labelimg", "-li", "-labelimg"};

    QString bestGuess;

    // 1) Öncelikli adlarla eşleşen kardeşleri dene
    for (const QString& key : preferKeys) {
        for (const QString& name : subdirs) {
            if (!name.startsWith("labels_", Qt::CaseInsensitive)) continue;
            if (name.compare(ourBase, Qt::CaseInsensitive) == 0) continue;
            if (!name.contains(key, Qt::CaseInsensitive)) continue;

            const QString candTrain = QDir(root).filePath(name + "/train");
            if (QFileInfo::exists(candTrain) && QDir(candTrain).exists()) {
                if (hasLabels(candTrain))
                    return QDir::toNativeSeparators(QFileInfo(candTrain).absoluteFilePath());
                if (bestGuess.isEmpty())
                    bestGuess = QDir::toNativeSeparators(QFileInfo(candTrain).absoluteFilePath());
            }
        }
    }

    // 2) Herhangi bir labels_* kardeşinde etiket dosyası var mı?
    for (const QString& name : subdirs) {
        if (!name.startsWith("labels_", Qt::CaseInsensitive)) continue;
        if (name.compare(ourBase, Qt::CaseInsensitive) == 0) continue;

        const QString candTrain = QDir(root).filePath(name + "/train");
        if (QFileInfo::exists(candTrain) && QDir(candTrain).exists() && hasLabels(candTrain))
            return QDir::toNativeSeparators(QFileInfo(candTrain).absoluteFilePath());
    }

    // 3) En iyi tahmini döndür (boş olabilir)
    return bestGuess;
}

// ========================== eklenen yardımcılar ==========================
namespace lu {

QString absNative(const QString& p) {
    return QDir::toNativeSeparators(QFileInfo(p).absoluteFilePath());
}

QString clean(const QString& p) {
    return QDir::cleanPath(p);
}

QString dirOfFile(const QString& fileAbs) {
    return QDir::toNativeSeparators(QFileInfo(fileAbs).absolutePath());
}

bool hasAtLeastNImages(const QString& dir, int n) {
    QDir d(dir);
    QStringList exts = {"*.jpg","*.jpeg","*.png","*.bmp","*.tif","*.tiff"};
    return d.entryList(exts, QDir::Files | QDir::Readable).size() >= n;
}

void logLaunch3(QPlainTextEdit* log, const LaunchTriplet& t) {
    if (!log) return;
    log->appendPlainText("[launchLabelImg] imagesDir = " + (t.imagesDir.isEmpty()  ? QString("<empty>") : t.imagesDir));
    log->appendPlainText("[launchLabelImg] classes   = " + (t.classesPath.isEmpty()? QString("<empty>") : t.classesPath));
    log->appendPlainText("[launchLabelImg] saveDir   = " + (t.saveDir.isEmpty()    ? QString("<empty>") : t.saveDir));
}

static QString pickLabelsFolder(const QString& baseRoot, bool isVOC, bool separateLI) {
    const char* yolo = separateLI ? "labels_yolo_li" : "labels_yolo";
    const char* voc  = separateLI ? "labels_voc_li"  : "labels_voc";
    return QDir::toNativeSeparators(QString("%1/%2/train").arg(baseRoot, isVOC ? voc : yolo));
}

LaunchTriplet preparePaths(const QString& openFileAbs,
                           const QString& classesTxt,
                           const QString& labelsDirUI,
                           const QString& format,
                           const QString& baseRoot,
                           bool separateLI)
{
    const bool isVOC = (format.compare("PascalVOC", Qt::CaseInsensitive) == 0);
    LaunchTriplet t;
    t.imagesDir   = dirOfFile(absNative(openFileAbs));
    t.classesPath = absNative(classesTxt);

    const QString forced = pickLabelsFolder(baseRoot, isVOC, separateLI);
    QDir().mkpath(forced);
    t.saveDir = forced;

    Q_UNUSED(labelsDirUI);
    return t;
}

// Mevcut global implementasyonu bozmadan ad-uyumlu sarmalayıcı
QString autoFindOtherLabels(const QString& oursTrainDir) {
    return ::autoFindOtherLabels(oursTrainDir);
}

} // namespace lu
