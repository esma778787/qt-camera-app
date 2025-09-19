// label_utils.h
#pragma once
#include <QString>

// İleri bildirim (compile time’ı hafif tutmak için)
class QPlainTextEdit;

namespace lu {

// Basit path yardımcıları
QString absNative(const QString& p);              // mutlak + native path
QString clean(const QString& p);                  // QDir::cleanPath
QString dirOfFile(const QString& fileAbs);        // dosya → klasör (abs)

// Klasörde yeterli görsel var mı?
bool hasAtLeastNImages(const QString& dir, int n = 2);

// 3 satır teşhis logu (imagesDir / classes / saveDir)
struct LaunchTriplet {
    QString imagesDir;
    QString classesPath;
    QString saveDir;
};
void logLaunch3(QPlainTextEdit* log, const LaunchTriplet& t);

// LabelImg için yolları hazırla (format=YOLO/PascalVOC)
// separateLI=true ⇒ labels_*_li/train (karşılaştırma için ayrık klasör)
LaunchTriplet preparePaths(const QString& openFileAbs,
                           const QString& classesTxt,
                           const QString& labelsDirUI,
                           const QString& format,
                           const QString& baseRoot,
                           bool separateLI = true);

// “Diğer etiket” otomatik bulma – global implementasyona sarmalayıcı
QString autoFindOtherLabels(const QString& oursTrainDir);

} // namespace lu

// ────────────────────────────────────────────────────────────────────────────
// Global fonksiyon bildirimi (mevcut implementasyonunla uyumlu):
// Bizdeki labels_*/train klasöründen, aynı kökteki "LabelImg" türevi bir
// kardeş etiket klasörünü (örn. labels_yolo_li/train) otomatik bulur.
// İçinde .txt/.xml yoksa en iyi tahmini döndürür; bulunamazsa "" döner.
QString autoFindOtherLabels(const QString& oursTrainDir);
