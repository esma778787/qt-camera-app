#include "annotatorwidget.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>     // Qt6 için (setEncoding)
#include <QXmlStreamWriter>     // PascalVOC: stream writer
#include <QPainter>
#include <QPen>
#include <QOperatingSystemVersion>
#include <QDebug>
#include <algorithm>
#include <cmath>

// YENİ: dock↔full host yönetimi için
#include <QDockWidget>
#include <QVBoxLayout>
#include <QLayout>
#include <QTimer>
#include <QMetaType>            // meta-type kayıtları

// ---------------------------
// Yardımcılar
// ---------------------------
static QString sanitizeDirLike(const QString& in)
{
    QString s = in.trimmed();

    // Windows'ta bazen FileDialog text'i tırnakla gelebiliyor
    if (s.startsWith('"') && s.endsWith('"') && s.size() >= 2) {
        s = s.mid(1, s.size()-2);
    }

    // Eğer bir "dosyayı" işaret ediyorsa, üst klasöre çık
    QFileInfo fi(s);
    if (fi.exists() && fi.isFile()) {
        s = fi.absolutePath();
    } else {
        // Uzantı benzeri varsa ve yol dizin değilse yine üst klasöre çek
        if (!fi.exists() && !fi.suffix().isEmpty()) {
            s = fi.absolutePath();
        }
    }

    // Normalize et
    s = QDir::cleanPath(s);
    s = QDir::toNativeSeparators(s);
    return s;
}

// Dosya-içi yardımcı
static inline QRectF clampRect(const QRectF& r, const QSize& s) {
    QRectF img(QPointF(0,0), s);
    return r.intersected(img);
}

// ---------------------------
// AnnotatorWidget
// ---------------------------
AnnotatorWidget::AnnotatorWidget(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(&m_scene);
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(QColor(20,20,20));
    m_pix = m_scene.addPixmap(QPixmap());

    // queued connection/moc tip çözümü için meta-type kayıtları
    qRegisterMetaType<AnnotatorWidget::Box>("AnnotatorWidget::Box");
    qRegisterMetaType<QVector<AnnotatorWidget::Box>>("QVector<AnnotatorWidget::Box>");

    // hareket/resize state varsayılanları
    m_sel           = -1;
    m_mode          = Mode::Idle;
    m_hot           = Handle::None;
    m_handlePx      = 7.0;      // handle kare yarıçapı değil; tam genişlik (widget px)
    m_minBoxPx      = 6.0;      // sahne pikselinde min kutu kenarı
    m_pressScene    = QPointF();
    m_startBoxScene = QRectF();
}

void AnnotatorWidget::setSaveDir(const QString& d)
{
    QString dir = sanitizeDirLike(d).trimmed();

    if (dir.isEmpty()) {
        m_saveDir.clear();
        qDebug() << "[Annotator] setSaveDir = \"\" (cleared; auto-pick on save)";
        return;
    }

    QFileInfo fi(dir);
    if (fi.exists() && fi.isFile()) {
        // Güvenlik: dosya seçilmiş → üst klasöre düzelt
        dir = fi.absolutePath();
    }
    if (!QDir().mkpath(dir)) {
        qWarning() << "[Annotator] setSaveDir: mkpath failed for" << dir;
    }

    m_saveDir = QDir::toNativeSeparators(dir);
    qDebug() << "[Annotator] setSaveDir =" << m_saveDir;
}

void AnnotatorWidget::setImageList(const QStringList& list, int startIndex)
{
    m_images = list;
    if (m_images.isEmpty()) {
        m_index = -1;
        m_imagePath.clear();
        m_boxes.clear();
        if (m_pix) m_pix->setPixmap(QPixmap());
        m_scene.setSceneRect(QRectF());
        viewport()->update();
        return;
    }
    m_index  = qBound(0, startIndex, m_images.size()-1);
    loadImage(m_images[m_index]);
}

bool AnnotatorWidget::loadImage(const QString& path)
{
    QPixmap px(path);
    if (px.isNull()) return false;
    m_imagePath = path;

    // görsel değişti → kutuları temizle & stem güncelle
    m_boxes.clear();
    m_currentStem = QFileInfo(m_imagePath).completeBaseName();

    if (m_pix) {
        m_pix->setPixmap(px);
        m_pix->setOffset(0,0);
    }
    m_scene.setSceneRect(QRectF(QPointF(0,0), px.size()));
    resetTransform();
    fitInView(m_pix, Qt::KeepAspectRatio);
    viewport()->update();

    emit boxesChanged(m_boxes, m_currentStem);
    return true;
}

QString AnnotatorWidget::currentImage() const
{
    if (m_index < 0 || m_index >= m_images.size()) return {};
    return m_images[m_index];
}

void AnnotatorWidget::setFormat(const QString& f)
{
    m_format = f.trimmed(); // "YOLO" / "PascalVOC"
}

void AnnotatorWidget::setActiveClass(const QString& c)
{
    // sınıf listesinden indeks bul; yoksa -1
    const int idx = m_classes.indexOf(c);
    m_currentClass = (idx >= 0) ? idx : -1;
}

void AnnotatorWidget::clearBoxes()
{
    m_boxes.clear();
    viewport()->update();
    emit boxesChanged(m_boxes, m_currentStem);
}

// =========================
// YENİ: Görsel listeleyici
// =========================
QStringList AnnotatorWidget::listImagesCaseInsensitive(const QString& dir)
{
    QStringList out;
    QDir d(dir);
    const QStringList files = d.entryList(QDir::Files, QDir::Name);
    for (const QString& f : files) {
        const QString ext = QFileInfo(f).suffix().toLower();
        if (QStringList{"png","jpg","jpeg","bmp","tif","tiff"}.contains(ext))
            out << d.absoluteFilePath(f);
    }
    return out;
}

// =========================
// YENİ: Klasör aç
// =========================
bool AnnotatorWidget::openDir(const QString& dirIn)
{
    const QString dir = sanitizeDirLike(dirIn);
    QStringList imgs = listImagesCaseInsensitive(dir);
    if (imgs.isEmpty()) {
        qWarning() << "[Annotator] openDir: no images in" << dir;
        return false;
    }
    setImageList(imgs, 0);      // setImageList zaten ilk görseli yükler
    return true;
}

// =========================
// YENİ: Dosyalar aç
// =========================
bool AnnotatorWidget::openFiles(const QStringList& filesIn)
{
    QStringList files = filesIn;
    files.erase(std::remove_if(files.begin(), files.end(), [](const QString& p){
                    const QString ext = QFileInfo(p).suffix().toLower();
                    return !QFileInfo::exists(p) ||
                           !QStringList{"png","jpg","jpeg","bmp","tif","tiff"}.contains(ext);
                }), files.end());
    std::sort(files.begin(), files.end(), [](const QString& a, const QString& b){
        return QFileInfo(a).fileName().toLower() < QFileInfo(b).fileName().toLower();
    });
    if (files.isEmpty()) {
        qWarning() << "[Annotator] openFiles: filtered empty list";
        return false;
    }
    setImageList(files, 0);      // setImageList zaten ilk görseli yükler
    return true;
}

// =========================
// YENİ: Sınıf dosyası yükleme
// =========================
bool AnnotatorWidget::loadClassesFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QStringList cls;
    QTextStream ts(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    ts.setEncoding(QStringConverter::Utf8);
#else
    ts.setCodec("UTF-8");
#endif
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        cls << line;
    }
    f.close();

    setClasses(cls);
    qDebug() << "[Annotator] classes loaded:" << m_classes;
    return !m_classes.isEmpty();
}

// =========================
// === HAREKET/RESIZE CORE ===
// =========================

// Handle dikdörtgenlerini widget (viewport) koordinatında üret
QVector<QRectF> AnnotatorWidget::handleRectsW(const QRectF& boxScene) const
{
    // köşe & kenar ortaları: TL, TR, BL, BR, L, R, T, B
    QVector<QPointF> pts;
    pts << boxScene.topLeft() << boxScene.topRight()
        << boxScene.bottomLeft() << boxScene.bottomRight()
        << QPointF(boxScene.left(), boxScene.center().y())
        << QPointF(boxScene.right(), boxScene.center().y())
        << QPointF(boxScene.center().x(), boxScene.top())
        << QPointF(boxScene.center().x(), boxScene.bottom());

    QVector<QRectF> out; out.reserve(8);
    for (const QPointF& pScene : pts) {
        QPoint pW = mapFromScene(pScene);
        out << QRectF(pW.x()-m_handlePx, pW.y()-m_handlePx, 2*m_handlePx, 2*m_handlePx);
    }
    return out;
}

AnnotatorWidget::Hit AnnotatorWidget::hitTest(const QPointF& wPos) const
{
    Hit h;

    // 1) Handle öncelikli (tüm kutular)
    for (int i=0; i<m_boxes.size(); ++i) {
        const QRectF bS = m_boxes[i].rect; // scene px
        auto rects = handleRectsW(bS);
        for (int k=0; k<rects.size(); ++k) {
            if (rects[k].contains(wPos)) {
                h.boxIndex = i;
                h.handle   = static_cast<Handle>(k);
                return h;
            }
        }
    }

    // 2) Kutu içi (üstteki önce)
    for (int i=m_boxes.size()-1; i>=0; --i) {
        const QRectF bS = m_boxes[i].rect;
        QRect wRect = mapFromScene(bS).boundingRect();
        if (wRect.contains(wPos.toPoint())) {
            h.boxIndex = i;
            h.handle   = Handle::None;
            return h;
        }
    }

    return h; // yok
}

void AnnotatorWidget::setCursorForHandle(Handle h)
{
    switch (h) {
    case Handle::TL:
    case Handle::BR: setCursor(Qt::SizeFDiagCursor); break;
    case Handle::TR:
    case Handle::BL: setCursor(Qt::SizeBDiagCursor); break;
    case Handle::L:
    case Handle::R:  setCursor(Qt::SizeHorCursor);   break;
    case Handle::T:
    case Handle::B:  setCursor(Qt::SizeVerCursor);   break;
    case Handle::None: default: setCursor(Qt::ArrowCursor); break;
    }
}

static inline void clampBoxScene(QRectF& r, const QSize& imgSize, qreal minPx)
{
    // Saha içinde tut ve min boyuta zorla
    r = r.normalized();
    QRectF img(QPointF(0,0), imgSize);
    r = r.intersected(img);

    if (r.width() < minPx)  r.setRight(r.left() + minPx);
    if (r.height() < minPx) r.setBottom(r.top() + minPx);

    // tekrar içeri kırp
    r = r.intersected(img);
}

// ---------------------------

void AnnotatorWidget::mousePressEvent(QMouseEvent* e)
{
    // Önce: handle/kutu hit-test (LabelImg davranışı)
    if (e->button() == Qt::LeftButton && m_pix && !m_pix->pixmap().isNull()) {
        const QPointF w = e->pos();
        Hit hit = hitTest(w);

        if (hit.boxIndex >= 0) {
            // Seçili kutu ve hareket/resize başlat
            m_sel           = hit.boxIndex;
            m_hot           = hit.handle;
            m_mode          = (m_hot == Handle::None) ? Mode::Moving : Mode::Resizing;
            m_pressScene    = mapToScene(e->pos());
            m_startBoxScene = m_boxes[m_sel].rect;

            if (m_mode == Mode::Moving) setCursor(Qt::SizeAllCursor);
            else                        setCursorForHandle(m_hot);

            e->accept();
            viewport()->update();
            return;
        }
    }

    // Hit yoksa: mevcut çizim akışın (yeni kutu oluşturma)
    if (e->button()==Qt::LeftButton && m_pix && !m_pix->pixmap().isNull()) {
        m_mode       = Mode::Creating;
        m_drawing    = true;
        m_sel        = -1;
        m_hot        = Handle::None;
        m_startScene = mapToScene(e->pos());
        m_lastScene  = m_startScene;
        e->accept();
        return;
    }

    QGraphicsView::mousePressEvent(e);
}

void AnnotatorWidget::mouseMoveEvent(QMouseEvent* e)
{
    // Creating (mevcut davranış)
    if (m_mode == Mode::Creating && m_drawing) {
        m_lastScene = mapToScene(e->pos());
        viewport()->update();
        e->accept();
        return;
    }

    // Moving
    if (m_mode == Mode::Moving && m_sel >= 0 && m_sel < m_boxes.size()) {
        const QPointF curS = mapToScene(e->pos());
        const QPointF d    = curS - m_pressScene;
        QRectF r           = m_startBoxScene.translated(d);
        clampBoxScene(r, m_pix->pixmap().size(), m_minBoxPx);
        m_boxes[m_sel].rect = r;
        viewport()->update();
        e->accept();
        return;
    }

    // Resizing
    if (m_mode == Mode::Resizing && m_sel >= 0 && m_sel < m_boxes.size()) {
        const QPointF curS = mapToScene(e->pos());
        QRectF r = m_startBoxScene;

        switch (m_hot) {
        case Handle::TL: r.setTopLeft(curS); break;
        case Handle::TR: r.setTopRight(curS); break;
        case Handle::BL: r.setBottomLeft(curS); break;
        case Handle::BR: r.setBottomRight(curS); break;
        case Handle::L:  r.setLeft(curS.x()); break;
        case Handle::R:  r.setRight(curS.x()); break;
        case Handle::T:  r.setTop(curS.y()); break;
        case Handle::B:  r.setBottom(curS.y()); break;
        default: break;
        }

        clampBoxScene(r, m_pix->pixmap().size(), m_minBoxPx);
        m_boxes[m_sel].rect = r;
        viewport()->update();
        e->accept();
        return;
    }

    // Idle: cursor feedback (handle üstünde imleç)
    if (m_mode == Mode::Idle) {
        Hit h = hitTest(e->pos());
        setCursorForHandle(h.handle);
    }

    QGraphicsView::mouseMoveEvent(e);
}

void AnnotatorWidget::mouseReleaseEvent(QMouseEvent* e)
{
    // Creating bitişi
    if (m_mode == Mode::Creating && m_drawing && e->button()==Qt::LeftButton) {
        m_drawing = false;
        QRectF r = QRectF(m_startScene, mapToScene(e->pos())).normalized();
        // min boyut filtresi
        if (r.width()>3 && r.height()>3) {
            // görüntü sınırları içinde kırp
            r = r.intersected(QRectF(QPointF(0,0), m_pix->pixmap().size()));
            clampBoxScene(r, m_pix->pixmap().size(), m_minBoxPx);
            if (r.isValid())
                m_boxes.push_back({r, m_currentClass});
        }
        viewport()->update();

        emit boxesChanged(m_boxes, m_currentStem);

        m_mode = Mode::Idle;
        e->accept();
        return;
    }

    // Move/Resize bitişi
    if ((m_mode == Mode::Moving || m_mode == Mode::Resizing) && e->button()==Qt::LeftButton) {
        m_mode = Mode::Idle;
        m_hot  = Handle::None;
        setCursor(Qt::ArrowCursor);
        viewport()->update();

        emit boxesChanged(m_boxes, m_currentStem);

        e->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(e);
}

void AnnotatorWidget::wheelEvent(QWheelEvent* e)
{
    // Mevcut zoom (AnchorUnderMouse aktif)
    const double s = std::pow(1.0015, e->angleDelta().y());
    scale(s, s);
    e->accept();
}

void AnnotatorWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->matches(QKeySequence::Save)) { saveCurrent(); return; }
    if (e->key()==Qt::Key_Delete && !m_boxes.isEmpty()) {
        // Seçili varsa onu sil; yoksa son ekleneni sil
        if (m_sel >= 0 && m_sel < m_boxes.size()) {
            m_boxes.removeAt(m_sel);
            m_sel = -1;
        } else {
            m_boxes.removeLast();
        }
        viewport()->update();

        emit boxesChanged(m_boxes, m_currentStem);
        return;
    }
    if (e->key()==Qt::Key_W)      { setDragMode(QGraphicsView::NoDrag); return; }            // çizim modu
    if (e->key()==Qt::Key_Space)  { setDragMode(QGraphicsView::ScrollHandDrag); return; }    // pan
    if (e->key()==Qt::Key_D)      { nextImage(); return; }
    if (e->key()==Qt::Key_A)      { prevImage(); return; }
    QGraphicsView::keyPressEvent(e);
}

void AnnotatorWidget::drawForeground(QPainter* p, const QRectF& r)
{
    Q_UNUSED(r);
    if (!m_pix) return;

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    QPen pen(Qt::white, 2.0);
    p->setPen(pen);

    // mevcut kutular
    for (int i=0; i<m_boxes.size(); ++i) {
        const auto& b = m_boxes[i];

        // Kutu
        if (i == m_sel) p->setPen(QPen(Qt::yellow, 2.0));
        else            p->setPen(QPen(Qt::white, 2.0));
        p->drawRect(b.rect);

        // Etiket arkaplanını sınıf adına göre dinamik genişlikte yap
        const QString cls = (b.cls>=0 && b.cls<m_classes.size())
                                ? m_classes[b.cls]
                                : QString("cls%1").arg(b.cls);
        QFontMetrics fm(p->font());
        const int w = fm.horizontalAdvance(cls) + 8;
        QRectF labelRect = QRectF(b.rect.topLeft() + QPointF(0,-18), QSizeF(w,18));

        p->fillRect(labelRect, QColor(0,0,0,180));
        p->setPen(Qt::white);
        p->drawText(labelRect.adjusted(3,0,-3,0), Qt::AlignVCenter|Qt::AlignLeft, cls);
    }

    // Seçili kutu için handle kareleri
    if (m_sel >= 0 && m_sel < m_boxes.size()) {
        auto handles = handleRectsW(m_boxes[m_sel].rect);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 0));

        // Widget rect → scene rect yaklaşık dönüşüm
        for (const QRectF& rw : handles) {
            QPointF sTL = mapToScene(rw.topLeft().toPoint());
            QPointF sBR = mapToScene(rw.bottomRight().toPoint());
            QRectF sRect(sTL, sBR);
            p->drawRect(sRect.normalized());
        }
    }

    // canlı çizim (creating)
    if (m_mode == Mode::Creating && m_drawing) {
        QRectF rr = QRectF(m_startScene, m_lastScene).normalized();
        p->setPen(QPen(Qt::cyan, 2.0, Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawRect(rr);
    }

    p->restore();
}

// ---- Kaydetme ----
bool AnnotatorWidget::saveCurrent()
{
    if (m_imagePath.isEmpty()) {
        qWarning() << "[Annotator] saveCurrent: imagePath empty";
        return false;
    }

    // Fallback: m_saveDir boşsa görüntü klasörü altında labels_* üret
    if (m_saveDir.isEmpty()) {
        const bool isVOC = (m_format == "PascalVOC");
        const QFileInfo fi(m_imagePath);
        QDir base(fi.absolutePath());
        const QString rel = isVOC ? "labels_voc/train" : "labels_yolo/train";
        QString autoDir = base.filePath(rel);
        autoDir = sanitizeDirLike(autoDir);
        if (!QDir().mkpath(autoDir)) {
            qWarning() << "[Annotator] saveCurrent: mkpath failed for" << autoDir;
            return false;
        }
        m_saveDir = QDir::toNativeSeparators(autoDir);
        qDebug() << "[Annotator] saveCurrent: auto saveDir =" << m_saveDir;
    } else {
        // Verilmiş saveDir’i de temizle/garantile
        m_saveDir = sanitizeDirLike(m_saveDir);
        if (!QDir().mkpath(m_saveDir)) {
            qWarning() << "[Annotator] saveCurrent: mkpath failed for" << m_saveDir;
            return false;
        }
    }

    // Ek log: ortam
    qDebug() << "[Annotator] OS =" << QOperatingSystemVersion::current().name();
    qDebug() << "[Annotator] IMG =" << m_imagePath;
    qDebug() << "[Annotator] DIR =" << m_saveDir;
    qDebug() << "[Annotator] FORMAT =" << m_format;

    // >>> ÖNEMLİ: Kaydet SONRASINDA **KUTULARI TEMİZLEME / NEXT'E GEÇME YOK**
    // (m_boxes.clear(); update(); nextImage();) gibi satırlar bilerek yok.

    if (m_format == "PascalVOC") return saveVOC(m_imagePath, m_saveDir);
    return saveYOLO(m_imagePath, m_saveDir);
}

bool AnnotatorWidget::saveYOLO(const QString& imgPath, const QString& outDir)
{
    const QPixmap& px = m_pix->pixmap();
    if (px.isNull()) {
        qWarning() << "[Annotator] saveYOLO: pixmap null";
        return false;
    }
    const double W = px.width(), H = px.height();

    QStringList lines;
    for (const auto& b : m_boxes) {
        if (b.cls < 0) continue;  // geçersiz sınıfı yazma
        QRectF r = clampRect(b.rect, px.size());
        const double xc = (r.center().x()) / W;
        const double yc = (r.center().y()) / H;
        const double ww = (r.width())      / W;
        const double hh = (r.height())     / H;
        lines << QString("%1 %2 %3 %4 %5")
                     .arg(b.cls)
                     .arg(xc,0,'f',6)
                     .arg(yc,0,'f',6)
                     .arg(ww,0,'f',6)
                     .arg(hh,0,'f',6);
    }

    const QString base = QFileInfo(imgPath).completeBaseName();
    QString outDirSan = sanitizeDirLike(outDir);
    const QString out  = QDir(outDirSan).filePath(base + ".txt");

    // Hedef klasör yoksa oluştur
    const QString outParent = QFileInfo(out).absolutePath();
    if (!QDir().mkpath(outParent)) {
        qWarning() << "[Annotator] saveYOLO: mkpath failed for" << outParent;
        return false;
    }

    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[Annotator] saveYOLO: cannot open file for write:" << out << "err=" << f.error();
        return false;
    }

    QTextStream ts(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    ts.setEncoding(QStringConverter::Utf8);
#else
    ts.setCodec("UTF-8");
#endif
    for (const QString& L : lines) ts << L << '\n';
    f.close();
    qDebug() << "[Annotator] saveYOLO: wrote" << out;

    // >>> İSTENEN LOG SATIRI (kaydettikten sonra kutular EKRANDA KALIR)
    emit log(QStringLiteral("[ours] saved: %1").arg(out));

    emit info(QStringLiteral("Saved YOLO: %1").arg(out));
    return true;
}

bool AnnotatorWidget::saveVOC(const QString& imgPath, const QString& outDir)
{
    const QPixmap& px = m_pix->pixmap();
    if (px.isNull()) {
        qWarning() << "[Annotator] saveVOC: pixmap null";
        return false;
    }

    const QString base = QFileInfo(imgPath).completeBaseName();
    QString outDirSan = sanitizeDirLike(outDir);
    const QString out  = QDir(outDirSan).filePath(base + ".xml");

    // Hedef klasör yoksa oluştur
    const QString outParent = QFileInfo(out).absolutePath();
    if (!QDir().mkpath(outParent)) {
        qWarning() << "[Annotator] saveVOC: mkpath failed for" << outParent;
        return false;
    }

    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[Annotator] saveVOC: cannot open file for write:" << out << "err=" << f.error();
        return false;
    }

    QXmlStreamWriter xml(&f);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();   // UTF-8 varsayılan

    xml.writeStartElement("annotation");

    // <size>
    xml.writeStartElement("size");
    xml.writeTextElement("width",  QString::number(px.width()));
    xml.writeTextElement("height", QString::number(px.height()));
    xml.writeTextElement("depth",  "3");
    xml.writeEndElement(); // size

    // <object>…</object>
    for (const auto& b : m_boxes) {
        if (b.cls < 0) continue;  // geçersiz sınıfı yazma
        QRectF r = clampRect(b.rect, px.size());
        const QString cls = (b.cls>=0 && b.cls<m_classes.size())
                                ? m_classes[b.cls]
                                : QString("cls%1").arg(b.cls);

        xml.writeStartElement("object");
        xml.writeTextElement("name", cls);

        xml.writeStartElement("bndbox");
        xml.writeTextElement("xmin", QString::number(int(r.left())));
        xml.writeTextElement("ymin", QString::number(int(r.top())));
        xml.writeTextElement("xmax", QString::number(int(r.right())));
        xml.writeTextElement("ymax", QString::number(int(r.bottom())));
        xml.writeEndElement(); // bndbox

        xml.writeEndElement(); // object
    }

    xml.writeEndElement(); // annotation
    xml.writeEndDocument();

    f.close();
    qDebug() << "[Annotator] saveVOC: wrote" << out;

    // >>> İSTENEN LOG SATIRI
    emit log(QStringLiteral("[ours] saved: %1").arg(out));

    emit info(QStringLiteral("Saved VOC: %1").arg(out));
    return true;
}

void AnnotatorWidget::nextImage()
{
    if (m_images.isEmpty()) return;
    if (m_index < m_images.size()-1) {
        ++m_index;
        loadImage(m_images[m_index]);
    }
}

void AnnotatorWidget::prevImage()
{
    if (m_images.isEmpty()) return;
    if (m_index > 0) {
        --m_index;
        loadImage(m_images[m_index]);
    }
}

// =========================
// Dock ↔ Full host yönetimi
// =========================
void AnnotatorWidget::setHosts(QDockWidget* dock,
                               QWidget* fullHost,
                               QWidget* menuFrame,
                               QWidget* videoHost)
{
    m_dock      = dock;
    m_fullHost  = fullHost;
    m_menuFrame = menuFrame;
    m_videoHost = videoHost;

    if (m_fullHost && !qobject_cast<QVBoxLayout*>(m_fullHost->layout())) {
        auto *vl = new QVBoxLayout(m_fullHost);
        vl->setContentsMargins(0,0,0,0);
        vl->setSpacing(0);
    }
    if (m_fullHost) m_fullHost->hide();
    emit log(QStringLiteral("[Annotator] setHosts ok"));
}

void AnnotatorWidget::enterFull()
{
    if (!m_dock || !m_fullHost) return;

    QWidget* dockContent = m_dock->widget();
    if (!dockContent) return;

    // İçeriği fullHost içine taşı
    dockContent->setParent(m_fullHost);
    if (auto *lay = qobject_cast<QVBoxLayout*>(m_fullHost->layout())) {
        while (lay->count() > 0) {
            if (QLayoutItem *it = lay->takeAt(0)) {
                if (QWidget *w = it->widget()) w->hide();
                delete it;
            }
        }
        lay->addWidget(dockContent);
    }

    // Dock'u boşalt & gizle (placeholder bırak)
    m_dock->setWidget(new QWidget(m_dock));
    m_dock->hide();

    if (m_menuFrame) m_menuFrame->hide();
    if (m_videoHost) m_videoHost->hide();

    m_fullHost->show();
    m_fullHost->raise();

    QTimer::singleShot(0, m_fullHost, [host=m_fullHost]{
        if (host->parentWidget()) host->parentWidget()->updateGeometry();
        host->updateGeometry();
    });

    emit log(QStringLiteral("[Annotator] enterFull"));
}

void AnnotatorWidget::exitFull()
{
    if (!m_dock || !m_fullHost) return;

    QWidget *dockContent = nullptr;
    if (auto *lay = qobject_cast<QVBoxLayout*>(m_fullHost->layout())) {
        if (lay->count() > 0) {
            if (QLayoutItem *it = lay->takeAt(0)) {
                dockContent = it->widget();
                delete it;
            }
        }
    }

    if (dockContent) {
        dockContent->setParent(m_dock);
        m_dock->setWidget(dockContent);
        m_dock->show();
        m_dock->raise();
    }

    m_fullHost->hide();

    if (m_videoHost) m_videoHost->show();
    if (m_menuFrame) m_menuFrame->show();

    QTimer::singleShot(0, m_fullHost, [host=m_fullHost]{
        if (host->parentWidget()) host->parentWidget()->updateGeometry();
        host->updateGeometry();
    });

    emit log(QStringLiteral("[Annotator] exitFull"));
}
