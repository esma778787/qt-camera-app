#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QStringList>
#include <QVector>
#include <QFileInfo>
#include <QRectF>
#include <QPointF>
#include <QtGlobal>      // qBound
#include <QMetaType>     // Q_DECLARE_METATYPE

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QPainter;
class QWidget;        // forward decl.
class QDockWidget;    // forward decl.

class AnnotatorWidget : public QGraphicsView
{
    Q_OBJECT
public:
    explicit AnnotatorWidget(QWidget* parent=nullptr);

    // --- Mevcut API ---
    bool     loadImage(const QString& path);
    void     clearBoxes();

    void     setClasses(const QStringList& classes) { m_classes = classes; }
    void     setCurrentClass(int idx) { m_currentClass = qBound(0, idx, m_classes.size()-1); }
    void     setFormat(const QString& f);
    void     setSaveDir(const QString& d);
    QString  currentImage() const;
    void     setActiveClass(const QString& c);

    // --- EKLENEN API (yapıyı bozmadan) ---
    Q_INVOKABLE bool openDir(const QString& dir);
    Q_INVOKABLE bool openFiles(const QStringList& files);
    bool            loadClassesFromFile(const QString& path);

    void     setActiveClassIndex(int idx) { setCurrentClass(idx); }
    int      currentClassIndex() const    { return m_currentClass; }

    static QStringList listImagesCaseInsensitive(const QString& dir);

    // Kutular (public)
    struct Box { QRectF rect; int cls = 0; };
    using Boxes = QVector<Box>;

public slots:
    bool saveCurrent();
    void nextImage();
    void prevImage();
    void setImageList(const QStringList& list, int startIndex=0);

    // --- Dock ↔ Full host yönetimi ---
    void setHosts(QDockWidget* dock,
                  QWidget* fullHost,
                  QWidget* menuFrame=nullptr,
                  QWidget* videoHost=nullptr);
    void enterFull();
    void exitFull();

signals:
    void boxesChanged(const QVector<Box>& boxes, const QString& stem);
    void log(const QString& line);
    void info(const QString& msg);

protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void drawForeground(QPainter* p, const QRectF& r) override;

private:
    bool saveYOLO(const QString& imgPath, const QString& outDir);
    bool saveVOC (const QString& imgPath, const QString& outDir);

    // ===========================
    // HAREKET / RESIZE yardımcıları
    // ===========================
    enum class Mode   { Idle, Creating, Moving, Resizing };
    enum class Handle { None=-1, TL=0, TR=1, BL=2, BR=3, L=4, R=5, T=6, B=7 };

    struct Hit {
        int    boxIndex = -1;
        Handle handle   = Handle::None;
    };

    // Widget (viewport) koordinatında handle dikdörtgenleri üretir
    QVector<QRectF> handleRectsW(const QRectF& boxScene) const;
    // Hit-test (widget pos → hangi kutu/handle?)
    Hit  hitTest(const QPointF& wPos) const;
    // İmleci handle tipine göre ayarla
    void setCursorForHandle(Handle h);

private:
    QGraphicsScene       m_scene;
    QGraphicsPixmapItem* m_pix = nullptr;

    QStringList m_classes;
    int         m_currentClass = 0;

    QString     m_format   = "YOLO";
    QString     m_saveDir;
    QString     m_imagePath;
    QString     m_currentStem;

    QStringList m_images;
    int         m_index  = -1;

    // çizim durumu (yeni kutu oluşturma)
    bool    m_drawing = false;
    QPointF m_startScene, m_lastScene;
    QVector<Box> m_boxes;

    // hareket/resize durumu
    int      m_sel  = -1;                 // seçili kutu index
    Mode     m_mode = Mode::Idle;         // aktif mod
    Handle   m_hot  = Handle::None;       // aktif handle
    qreal    m_handlePx = 7.0;            // viewport’ta handle yarı-boyu
    qreal    m_minBoxPx = 6.0;            // sahne pikselinde min kutu kenarı
    QPointF  m_pressScene;                // mouse press sahne noktası
    QRectF   m_startBoxScene;             // sürükleme öncesi kutu

    // host referansları
    QDockWidget* m_dock      = nullptr;
    QWidget*     m_fullHost  = nullptr;
    QWidget*     m_menuFrame = nullptr;
    QWidget*     m_videoHost = nullptr;
};

// Meta-type deklarasyonları sınıf DIŞINDA olmalı
Q_DECLARE_METATYPE(AnnotatorWidget::Box)
Q_DECLARE_METATYPE(QVector<AnnotatorWidget::Box>)
